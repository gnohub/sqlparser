#include <ctype.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <jansson.h>

#include "../dialect/sqlparser_dialect_dml_result_internal.h"
#include "../dialect/sqlparser_dialect_internal.h"
#include "../dialect/sqlparser_dialect_multi_insert_internal.h"
#include "sqlparser_ast_internal.h"
#include "sqlparser_bind_occurrence_internal.h"
#include "sqlparser_control_internal.h"

typedef struct {
	char *name;
	sqlparser_bind_kind_t kind;
	size_t position;
} sqlparser_view_bind_info_t;

typedef struct {
	PgQuery__ParamRef *param_ref;
	size_t traversal_index;
	size_t statement_index;
	int32_t original_number;
	int32_t marker_number;
	size_t position;
	char *public_sql;
} sqlparser_view_bind_position_entry_t;

typedef struct {
	uintptr_t pointer;
	size_t entry_index;
} sqlparser_view_bind_pointer_entry_t;

typedef struct {
	size_t entry_index;
	const char *public_sql;
	size_t statement_index;
	int32_t original_number;
	size_t traversal_index;
} sqlparser_view_bind_missing_entry_t;

typedef struct {
	const char *public_sql;
	size_t offset;
	size_t count;
	size_t used;
} sqlparser_view_bind_missing_group_t;

typedef struct {
	sqlparser_view_bind_position_entry_t *entries;
	sqlparser_view_bind_pointer_entry_t *pointer_entries;
	size_t count;
	size_t capacity;
	int32_t marker_start;
	int built;
} sqlparser_view_bind_position_cache_t;

typedef struct {
	size_t offset;
	size_t length;
} sqlparser_view_control_unit_span_t;

typedef struct {
	sqlparser_handle_t *handle;
	size_t statement_index;
} sqlparser_view_build_t;

typedef struct {
	const void *pointer;
	size_t index;
} sqlparser_graph_pointer_index_t;

typedef struct {
	sqlparser_handle_t *handle;
	size_t statement_index;
	size_t seen;
	size_t target_index;
	int want_target;
	sqlparser_clause_kind_t target_kind;
	PgQuery__Node *target_expr;
} sqlparser_view_readonly_clause_search_t;

static int sqlparser_text_equal_ci(const char *left, const char *right)
{
	if (left == NULL || right == NULL) {
		return left == right;
	}
	while (*left != '\0' && *right != '\0') {
		if (tolower((unsigned char)*left) != tolower((unsigned char)*right)) {
			return 0;
		}
		left++;
		right++;
	}
	return *left == '\0' && *right == '\0';
}

typedef struct {
	int known;
	int quoted;
	int delimited;
} sqlparser_identifier_source_t;

typedef struct {
	sqlparser_identifier_source_t database;
	sqlparser_identifier_source_t schema;
	sqlparser_identifier_source_t object;
	sqlparser_identifier_source_t alias;
} sqlparser_graph_relation_identifier_t;

static int sqlparser_identifier_spelling_is_delimited(
	const char *spelling,
	size_t length)
{
	if (spelling == NULL || length < 2U) {
		return 0;
	}
	return (spelling[0] == '"' && spelling[length - 1U] == '"') ||
	       (spelling[0] == '`' && spelling[length - 1U] == '`') ||
	       (spelling[0] == '[' && spelling[length - 1U] == ']');
}

static int sqlparser_identifier_spelling_is_quoted(
	const char *spelling,
	size_t length)
{
	return spelling != NULL && length > 0U &&
		(spelling[0] == '"' || spelling[0] == '`' ||
		 spelling[0] == '[' ||
		 (length > 2U &&
		  (spelling[0] == 'U' || spelling[0] == 'u') &&
		  spelling[1] == '&' && spelling[2] == '"'));
}

static size_t sqlparser_identifier_skip_trivia(
	const char *sql,
	size_t length,
	size_t position)
{
	for (;;) {
		while (position < length &&
		       isspace((unsigned char)sql[position])) {
			position++;
		}
		if (position + 1U < length &&
		    sql[position] == '-' &&
		    sql[position + 1U] == '-') {
			position += 2U;
			while (position < length && sql[position] != '\n') {
				position++;
			}
			continue;
		}
		if (position + 1U < length &&
		    sql[position] == '/' &&
		    sql[position + 1U] == '*') {
			size_t depth;

			position += 2U;
			depth = 1U;
			while (position + 1U < length && depth > 0U) {
				if (sql[position] == '/' &&
				    sql[position + 1U] == '*') {
					depth++;
					position += 2U;
				} else if (sql[position] == '*' &&
					   sql[position + 1U] == '/') {
					depth--;
					position += 2U;
				} else {
					position++;
				}
			}
			continue;
		}
		return position;
	}
}

static int sqlparser_identifier_token(
	const char *sql,
	size_t length,
	size_t position,
	size_t *out_end,
	int *out_quoted)
{
	size_t index;

	if (sql == NULL || position >= length ||
	    out_end == NULL || out_quoted == NULL) {
		return 0;
	}
	index = position;
	if ((sql[index] == 'U' || sql[index] == 'u') &&
	    index + 2U < length &&
	    sql[index + 1U] == '&' &&
	    sql[index + 2U] == '"') {
		index += 3U;
		*out_quoted = 1;
	} else if (sql[index] == '"') {
		index++;
		*out_quoted = 1;
	} else {
		unsigned char c;

		c = (unsigned char)sql[index];
		if (!(isalnum(c) || c == '_' || c == '$' || c >= 0x80U)) {
			return 0;
		}
		index++;
		while (index < length) {
			c = (unsigned char)sql[index];
			if (!(isalnum(c) || c == '_' || c == '$' || c >= 0x80U)) {
				break;
			}
			index++;
		}
		*out_end = index;
		*out_quoted = 0;
		return 1;
	}
	while (index < length) {
		if (sql[index] == '"') {
			if (index + 1U < length && sql[index + 1U] == '"') {
				index += 2U;
				continue;
			}
			*out_end = index + 1U;
			return 1;
		}
		index++;
	}
	return 0;
}

static int sqlparser_identifier_component_source(
	const sqlparser_handle_t *handle,
	int location,
	size_t component_index,
	sqlparser_identifier_source_t *out_source,
	size_t *out_end)
{
	const char *spelling;
	const char *sql;
	sqlparser_proto_identifier_style_t style;
	size_t index;
	size_t length;
	size_t position;
	size_t spelling_length;

	if (out_source == NULL) {
		return 0;
	}
	memset(out_source, 0, sizeof(*out_source));
	spelling = NULL;
	spelling_length = 0U;
	if (handle != NULL &&
	    sqlparser_handle_identifier_spelling(
		    handle,
		    location,
		    component_index,
		    &spelling,
		    &spelling_length) &&
	    spelling_length > 0U) {
		out_source->known = 1;
		out_source->quoted = sqlparser_identifier_spelling_is_quoted(
			spelling,
			spelling_length);
		out_source->delimited =
			sqlparser_identifier_spelling_is_delimited(
				spelling,
				spelling_length);
		return 1;
	}
	style = sqlparser_proto_identifier_style(location, component_index);
	if (style != SQLPARSER_PROTO_IDENTIFIER_STYLE_NONE) {
		out_source->known = 1;
		out_source->quoted =
			style != SQLPARSER_PROTO_IDENTIFIER_STYLE_UNQUOTED;
		out_source->delimited = 0;
		return 1;
	}
	if (handle == NULL || location < 0) {
		return 0;
	}
	sql = handle->parser_sql;
	if (sql == NULL) {
		return 0;
	}
	length = handle->parser_sql_len;
	position = (size_t)location;
	if (position >= length) {
		return 0;
	}
	for (index = 0U; index <= component_index; index++) {
		size_t token_end;
		int quoted;

		position = sqlparser_identifier_skip_trivia(
			sql,
			length,
			position);
		if (!sqlparser_identifier_token(
			    sql,
			    length,
			    position,
			    &token_end,
			    &quoted)) {
			return 0;
		}
		if (index == component_index) {
			sqlparser_identifier_origin_t origin;

			out_source->known = 1;
			out_source->quoted = quoted;
			out_source->delimited =
				sqlparser_identifier_spelling_is_delimited(
					sql + position,
					token_end - position);
			if (handle->identifier_origins != NULL) {
				out_source->delimited = 0;
				if (sqlparser_identifier_origin_map_lookup(
					    handle->identifier_origins,
					    position,
					    token_end - position,
					    &origin) ==
					    SQLPARSER_IDENTIFIER_ORIGIN_SOURCE &&
				    origin.source_offset <= handle->sql_len &&
				    origin.source_length <=
					    handle->sql_len - origin.source_offset) {
					out_source->delimited =
						sqlparser_identifier_spelling_is_delimited(
							handle->sql + origin.source_offset,
							origin.source_length);
				}
			}
			if (out_end != NULL) {
				*out_end = token_end;
			}
			return 1;
		}
		position = sqlparser_identifier_skip_trivia(
			sql,
			length,
			token_end);
		if (position >= length || sql[position] != '.') {
			return 0;
		}
		position++;
	}
	return 0;
}

static const sqlparser_identifier_mutation_t *
sqlparser_identifier_mutation_for_source_slot(
	const sqlparser_handle_t *handle,
	const char *const *slot)
{
	size_t index;

	if (handle == NULL || slot == NULL) {
		return NULL;
	}
	for (index = 0U; index < handle->identifier_mutation_count; index++) {
		if ((const char *const *)handle->identifier_mutations[index].slot ==
		    slot) {
			return &handle->identifier_mutations[index];
		}
	}
	return NULL;
}

static int sqlparser_identifier_default_spelling_is_quoted(
	const char *identifier)
{
	size_t index;
	size_t length;

	if (identifier == NULL || identifier[0] == '\0' ||
	    !((identifier[0] >= 'a' && identifier[0] <= 'z') ||
	      identifier[0] == '_')) {
		return 1;
	}
	length = strlen(identifier);
	for (index = 0U; index < length; index++) {
		if (!((identifier[index] >= 'a' && identifier[index] <= 'z') ||
		      (identifier[index] >= '0' && identifier[index] <= '9') ||
		      identifier[index] == '_')) {
			return 1;
		}
	}
	return postgres_deparse_keyword_category(identifier, length) > 0;
}

static int sqlparser_current_identifier_source(
	const sqlparser_handle_t *handle,
	const char *const *slot,
	int location,
	size_t component_index,
	sqlparser_identifier_source_t *out_source)
{
	const sqlparser_identifier_mutation_t *mutation;
	const char *value;
	size_t spelling_length;

	if (out_source == NULL) {
		return 0;
	}
	memset(out_source, 0, sizeof(*out_source));
	value = slot != NULL ? *slot : NULL;
	mutation = sqlparser_identifier_mutation_for_source_slot(handle, slot);
	if (mutation != NULL && mutation->spelling != NULL &&
	    mutation->spelling[0] != '\0') {
		spelling_length = strlen(mutation->spelling);
		out_source->known = 1;
		out_source->quoted = sqlparser_identifier_spelling_is_quoted(
			mutation->spelling,
			spelling_length);
		out_source->delimited =
			sqlparser_identifier_spelling_is_delimited(
				mutation->spelling,
				spelling_length);
		return 1;
	}
	if (mutation != NULL && mutation->original != NULL &&
	    value != NULL && strcmp(value, mutation->original) != 0) {
		out_source->known = 1;
		out_source->quoted =
			sqlparser_identifier_default_spelling_is_quoted(value);
		out_source->delimited = 0;
		return 1;
	}
	if (mutation != NULL && mutation->source_present &&
	    mutation->has_source_component) {
		component_index = mutation->source_component_index;
	}
	return sqlparser_identifier_component_source(
		handle,
		location,
		component_index,
		out_source,
		NULL);
}

static int sqlparser_identifier_token_is_word(
	const char *sql,
	size_t start,
	size_t end,
	const char *word)
{
	size_t index;
	size_t word_length;

	if (sql == NULL || word == NULL || end < start) {
		return 0;
	}
	word_length = strlen(word);
	if (end - start != word_length) {
		return 0;
	}
	for (index = 0U; index < word_length; index++) {
		if (tolower((unsigned char)sql[start + index]) !=
		    tolower((unsigned char)word[index])) {
			return 0;
		}
	}
	return 1;
}

static void sqlparser_range_var_identifier_sources(
	const sqlparser_handle_t *handle,
	const PgQuery__RangeVar *range_var,
	sqlparser_graph_relation_identifier_t *out_identifiers)
{
	const char *object_spelling;
	size_t component_count;
	size_t object_spelling_length;

	if (out_identifiers == NULL) {
		return;
	}
	memset(out_identifiers, 0, sizeof(*out_identifiers));
	if (handle == NULL || range_var == NULL ||
	    range_var->relname == NULL) {
		return;
	}
	component_count =
		(range_var->catalogname != NULL &&
		 range_var->catalogname[0] != '\0' ? 1U : 0U) +
		(range_var->schemaname != NULL &&
		 range_var->schemaname[0] != '\0' ? 1U : 0U) +
		1U;
	(void)sqlparser_current_identifier_source(
		handle,
		(const char *const *)&range_var->relname,
		range_var->location,
		component_count - 1U,
		&out_identifiers->object);
	object_spelling = NULL;
	(void)sqlparser_dialect_relation_object_name(
		handle->dialect_ops,
		handle->dialect_state,
		range_var->relname,
		&object_spelling);
	if (object_spelling != NULL && object_spelling[0] != '\0') {
		object_spelling_length = strlen(object_spelling);
		out_identifiers->object.known = 1;
		out_identifiers->object.quoted =
			sqlparser_identifier_spelling_is_quoted(
				object_spelling,
				object_spelling_length);
		out_identifiers->object.delimited =
			sqlparser_identifier_spelling_is_delimited(
				object_spelling,
				object_spelling_length);
	}
	if (range_var->schemaname != NULL &&
	    range_var->schemaname[0] != '\0') {
		(void)sqlparser_current_identifier_source(
			handle,
			(const char *const *)&range_var->schemaname,
			range_var->location,
			range_var->catalogname != NULL &&
				range_var->catalogname[0] != '\0' ? 1U : 0U,
			&out_identifiers->schema);
	}
	if (range_var->catalogname != NULL &&
	    range_var->catalogname[0] != '\0') {
		(void)sqlparser_current_identifier_source(
			handle,
			(const char *const *)&range_var->catalogname,
			range_var->location,
			0U,
			&out_identifiers->database);
	}
	if (range_var->alias == NULL ||
	    range_var->alias->aliasname == NULL ||
	    range_var->alias->aliasname[0] == '\0') {
		return;
	}
	(void)sqlparser_current_identifier_source(
		handle,
		(const char *const *)&range_var->alias->aliasname,
		range_var->alias->location,
		0U,
		&out_identifiers->alias);
}

static int sqlparser_identifier_semantic_equal(
	const sqlparser_handle_t *handle,
	const char *left,
	sqlparser_identifier_source_t left_source,
	const char *right,
	sqlparser_identifier_source_t right_source)
{
	size_t index;
	size_t left_length;
	size_t right_length;
	int fold_unquoted_upper;
	int truncate_postgresql;

	if (left == NULL || right == NULL) {
		return left == right;
	}
	fold_unquoted_upper = 0;
	truncate_postgresql = 0;
	if (handle != NULL) {
		switch (handle->dialect) {
			case SQLPARSER_DIALECT_POSTGRESQL:
			case SQLPARSER_DIALECT_VASTBASE_POSTGRESQL:
				truncate_postgresql = 1;
				break;
			case SQLPARSER_DIALECT_ORACLE:
			case SQLPARSER_DIALECT_DAMENG:
			case SQLPARSER_DIALECT_VASTBASE_ORACLE:
				fold_unquoted_upper = 1;
				break;
			default:
				return sqlparser_text_equal_ci(left, right);
		}
	} else {
		return sqlparser_text_equal_ci(left, right);
	}
	if (!left_source.known || !right_source.known) {
		return strcmp(left, right) == 0;
	}
	left_length = strlen(left);
	right_length = strlen(right);
	if (truncate_postgresql && left_length > 63U) {
		left_length = 63U;
	}
	if (truncate_postgresql && right_length > 63U) {
		right_length = 63U;
	}
	if (left_length != right_length) {
		return 0;
	}
	for (index = 0U; index < left_length; index++) {
		unsigned char left_char;
		unsigned char right_char;

		left_char = (unsigned char)left[index];
		right_char = (unsigned char)right[index];
		if (!left_source.quoted) {
			if (fold_unquoted_upper &&
			    left_char >= 'a' && left_char <= 'z') {
				left_char =
					(unsigned char)(left_char - ('a' - 'A'));
			} else if (!fold_unquoted_upper &&
				   left_char >= 'A' && left_char <= 'Z') {
				left_char =
					(unsigned char)(left_char + ('a' - 'A'));
			}
		}
		if (!right_source.quoted) {
			if (fold_unquoted_upper &&
			    right_char >= 'a' && right_char <= 'z') {
				right_char =
					(unsigned char)(right_char - ('a' - 'A'));
			} else if (!fold_unquoted_upper &&
				   right_char >= 'A' && right_char <= 'Z') {
				right_char =
					(unsigned char)(right_char + ('a' - 'A'));
			}
		}
		if (left_char != right_char) {
			return 0;
		}
	}
	return 1;
}

static int sqlparser_identifier_token_value_equal(
	const char *sql,
	size_t start,
	size_t end,
	int quoted,
	const char *value)
{
	size_t value_index;

	if (sql == NULL || value == NULL || end <= start) {
		return 0;
	}
	if (!quoted) {
		return strlen(value) == end - start &&
			memcmp(sql + start, value, end - start) == 0;
	}
	if ((sql[start] == 'U' || sql[start] == 'u') &&
	    start + 2U < end &&
	    sql[start + 1U] == '&' &&
	    sql[start + 2U] == '"') {
		start += 3U;
	} else if (sql[start] == '"') {
		start++;
	} else {
		return 0;
	}
	if (end <= start || sql[end - 1U] != '"') {
		return 0;
	}
	end--;
	value_index = 0U;
	while (start < end) {
		char c;

		c = sql[start++];
		if (c == '"' && start < end && sql[start] == '"') {
			start++;
		}
		if (c == '\\') {
			return 0;
		}
		if (value[value_index] == '\0' ||
		    value[value_index] != c) {
			return 0;
		}
		value_index++;
	}
	return value[value_index] == '\0';
}

static sqlparser_identifier_source_t sqlparser_identifier_source_for_text(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	const char *value)
{
	sqlparser_identifier_source_t source;
	const char *sql;
	PgQuery__RawStmt *raw_stmt;
	size_t ast_statement_index;
	size_t end;
	size_t length;
	size_t position;

	memset(&source, 0, sizeof(source));
	if (handle == NULL || value == NULL || value[0] == '\0') {
		return source;
	}
	sql = sqlparser_effective_parser_sql(handle);
	if (sql == NULL) {
		return source;
	}
	length = strlen(sql);
	position = 0U;
	end = length;
	ast_statement_index = statement_index;
	if (handle->control != NULL &&
	    statement_index < handle->control->unit_count) {
		ast_statement_index =
			handle->control->units[statement_index].ast_statement_index;
	}
	raw_stmt = handle->ast != NULL &&
		ast_statement_index < handle->ast->n_stmts ?
		handle->ast->stmts[ast_statement_index] : NULL;
	if (raw_stmt != NULL && raw_stmt->stmt_location >= 0 &&
	    (size_t)raw_stmt->stmt_location < length) {
		position = (size_t)raw_stmt->stmt_location;
		if (raw_stmt->stmt_len > 0 &&
		    (size_t)raw_stmt->stmt_len <= length - position) {
			end = position + (size_t)raw_stmt->stmt_len;
		}
	}
	while (position < end) {
		size_t token_end;
		int quoted;

		position = sqlparser_identifier_skip_trivia(
			sql,
			end,
			position);
		if (position >= end) {
			break;
		}
		if (sql[position] == '\'') {
			position++;
			while (position < end) {
				if (sql[position] == '\'' &&
				    position + 1U < end &&
				    sql[position + 1U] == '\'') {
					position += 2U;
					continue;
				}
				if (sql[position++] == '\'') {
					break;
				}
			}
			continue;
		}
		if (sqlparser_identifier_token(
			    sql,
			    end,
			    position,
			    &token_end,
			    &quoted)) {
			if (sqlparser_identifier_token_value_equal(
				    sql,
				    position,
				    token_end,
				    quoted,
				    value)) {
				if (source.known &&
				    source.quoted != quoted) {
					memset(&source, 0, sizeof(source));
					return source;
				}
				source.known = 1;
				source.quoted = quoted;
			}
			position = token_end;
			continue;
		}
		position++;
	}
	return source;
}

static void sqlparser_view_copy_public_text(
	char *dst,
	size_t dst_size,
	const char *src,
	int *out_truncated)
{
	size_t len;
	size_t copy_len;

	if (out_truncated != NULL) {
		*out_truncated = 0;
	}
	if (dst == NULL || dst_size == 0U) {
		if (out_truncated != NULL && src != NULL && src[0] != '\0') {
			*out_truncated = 1;
		}
		return;
	}
	dst[0] = '\0';
	if (src == NULL || src[0] == '\0') {
		return;
	}
	len = strlen(src);
	copy_len = len < dst_size ? len : dst_size - 1U;
	if (copy_len > 0U) {
		memcpy(dst, src, copy_len);
	}
	dst[copy_len] = '\0';
	if (out_truncated != NULL && copy_len < len) {
		*out_truncated = 1;
	}
}

static int sqlparser_json_set_optional_string(json_t *object, const char *key, const char *value)
{
	json_t *item;

	if (object == NULL || key == NULL) {
		return -1;
	}
	if (value == NULL || value[0] == '\0') {
		return 0;
	}
	item = json_string(value);
	if (item == NULL) {
		return -1;
	}
	return json_object_set_new(object, key, item);
}

static int sqlparser_json_set_size(json_t *object, const char *key, size_t value)
{
	return json_object_set_new(object, key, json_integer((json_int_t)value));
}

static int sqlparser_json_set_optional_size(json_t *object, const char *key, int has_value, size_t value)
{
	if (object == NULL || key == NULL) {
		return -1;
	}
	return has_value ? json_object_set_new(object, key, json_integer((json_int_t)value)) : 0;
}

static int sqlparser_json_set_optional_selector(
	json_t *object,
	const char *key,
	const sqlparser_selector_t *selector,
	sqlparser_error_t *out_error)
{
	char *selector_text;
	sqlparser_status_t status;
	int rc;

	if (selector == NULL || selector->kind == SQLPARSER_SELECTOR_KIND_UNKNOWN) {
		return 0;
	}

	selector_text = NULL;
	status = sqlparser_selector_format(selector, &selector_text, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return -1;
	}
	rc = json_object_set_new(object, key, json_string(selector_text));
	sqlparser_string_free(selector_text);
	return rc;
}

static int sqlparser_json_set_nonempty_array(json_t *object, const char *key, json_t **array)
{
	if (object == NULL || key == NULL || array == NULL || *array == NULL) {
		return -1;
	}
	if (json_array_size(*array) == 0U) {
		json_decref(*array);
		*array = NULL;
		return 0;
	}
	if (json_object_set_new(object, key, *array) != 0) {
		*array = NULL;
		return -1;
	}
	*array = NULL;
	return 0;
}

static int sqlparser_json_array_append_owned(json_t *array, json_t **item)
{
	if (array == NULL || item == NULL || *item == NULL) {
		return -1;
	}
	if (json_array_append_new(array, *item) != 0) {
		*item = NULL;
		return -1;
	}
	*item = NULL;
	return 0;
}

static int sqlparser_json_object_set_owned(json_t *object, const char *key, json_t **item)
{
	if (object == NULL || key == NULL || item == NULL || *item == NULL) {
		return -1;
	}
	if (json_object_set_new(object, key, *item) != 0) {
		*item = NULL;
		return -1;
	}
	*item = NULL;
	return 0;
}

static char *sqlparser_view_ascii_upper_dup(const char *text)
{
	char *copy;
	size_t index;
	size_t len;

	if (text == NULL || text[0] == '\0') {
		return NULL;
	}
	len = strlen(text);
	copy = (char *)malloc(len + 1U);
	if (copy == NULL) {
		return NULL;
	}
	for (index = 0U; index < len; index++) {
		copy[index] = (char)toupper((unsigned char)text[index]);
	}
	copy[len] = '\0';
	return copy;
}

static int sqlparser_view_readonly_clause_record(
	sqlparser_view_readonly_clause_search_t *search,
	sqlparser_clause_kind_t kind,
	PgQuery__Node *expr)
{
	if (search == NULL || expr == NULL) {
		return 0;
	}
	if (search->want_target && search->seen == search->target_index) {
		search->target_kind = kind;
		search->target_expr = expr;
		return 1;
	}
	search->seen++;
	return 0;
}

static int sqlparser_view_readonly_clause_walk_select(
	sqlparser_view_readonly_clause_search_t *search,
	PgQuery__SelectStmt *stmt);

static int sqlparser_view_readonly_clause_walk_from_item(
	sqlparser_view_readonly_clause_search_t *search,
	PgQuery__Node *node)
{
	if (node == NULL) {
		return 0;
	}
	if (search->want_target && search->target_expr != NULL) {
		return 1;
	}
	switch (node->node_case) {
		case PG_QUERY__NODE__NODE_JOIN_EXPR:
			if (node->join_expr == NULL) {
				return 0;
			}
			if (sqlparser_view_readonly_clause_walk_from_item(search, node->join_expr->larg) ||
			    sqlparser_view_readonly_clause_walk_from_item(search, node->join_expr->rarg)) {
				return 1;
			}
			return sqlparser_view_readonly_clause_record(search, SQLPARSER_CLAUSE_KIND_ON, node->join_expr->quals);
		case PG_QUERY__NODE__NODE_RANGE_SUBSELECT:
			return node->range_subselect != NULL &&
				node->range_subselect->subquery != NULL &&
				node->range_subselect->subquery->node_case == PG_QUERY__NODE__NODE_SELECT_STMT ?
				sqlparser_view_readonly_clause_walk_select(search, node->range_subselect->subquery->select_stmt) :
				0;
		default:
			return 0;
	}
}

static int sqlparser_view_readonly_clause_walk_from_clause(
	sqlparser_view_readonly_clause_search_t *search,
	PgQuery__Node **items,
	size_t count)
{
	size_t index;

	for (index = 0U; index < count; index++) {
		if (sqlparser_view_readonly_clause_walk_from_item(search, items[index])) {
			return 1;
		}
	}
	return 0;
}

static int sqlparser_view_readonly_clause_walk_select(
	sqlparser_view_readonly_clause_search_t *search,
	PgQuery__SelectStmt *stmt)
{
	size_t index;

	if (stmt == NULL) {
		return 0;
	}
	if (search->want_target && search->target_expr != NULL) {
		return 1;
	}
	if (stmt->with_clause != NULL) {
		for (index = 0U; index < stmt->with_clause->n_ctes; index++) {
			PgQuery__Node *cte_node;

			cte_node = stmt->with_clause->ctes[index];
			if (cte_node != NULL &&
			    cte_node->node_case == PG_QUERY__NODE__NODE_COMMON_TABLE_EXPR &&
			    cte_node->common_table_expr != NULL &&
			    cte_node->common_table_expr->ctequery != NULL &&
			    cte_node->common_table_expr->ctequery->node_case == PG_QUERY__NODE__NODE_SELECT_STMT &&
			    sqlparser_view_readonly_clause_walk_select(search, cte_node->common_table_expr->ctequery->select_stmt)) {
				return 1;
			}
		}
	}
	if (stmt->larg != NULL || stmt->rarg != NULL) {
		return sqlparser_view_readonly_clause_walk_select(search, stmt->larg) ||
			sqlparser_view_readonly_clause_walk_select(search, stmt->rarg);
	}
	if (sqlparser_view_readonly_clause_walk_from_clause(search, stmt->from_clause, stmt->n_from_clause)) {
		return 1;
	}
	for (index = 0U; index < stmt->n_group_clause; index++) {
		if (sqlparser_view_readonly_clause_record(search, SQLPARSER_CLAUSE_KIND_GROUP_BY, stmt->group_clause[index])) {
			return 1;
		}
	}
	return sqlparser_view_readonly_clause_record(search, SQLPARSER_CLAUSE_KIND_HAVING, stmt->having_clause);
}

static int sqlparser_view_readonly_clause_walk_merge(
	sqlparser_view_readonly_clause_search_t *search,
	PgQuery__MergeStmt *stmt)
{
	if (stmt == NULL) {
		return 0;
	}
	if (sqlparser_view_readonly_clause_record(search, SQLPARSER_CLAUSE_KIND_ON, stmt->join_condition)) {
		return 1;
	}
	return sqlparser_view_readonly_clause_walk_from_item(search, stmt->source_relation);
}

static int sqlparser_view_readonly_clause_walk_statement(
	sqlparser_view_readonly_clause_search_t *search,
	PgQuery__Node *statement)
{
	if (statement == NULL) {
		return 0;
	}
	switch (statement->node_case) {
		case PG_QUERY__NODE__NODE_SELECT_STMT:
			return sqlparser_view_readonly_clause_walk_select(search, statement->select_stmt);
		case PG_QUERY__NODE__NODE_INSERT_STMT:
			return statement->insert_stmt != NULL &&
					statement->insert_stmt->select_stmt != NULL &&
					statement->insert_stmt->select_stmt->node_case == PG_QUERY__NODE__NODE_SELECT_STMT ?
				sqlparser_view_readonly_clause_walk_select(search, statement->insert_stmt->select_stmt->select_stmt) :
				0;
		case PG_QUERY__NODE__NODE_UPDATE_STMT:
			return statement->update_stmt != NULL ?
				sqlparser_view_readonly_clause_walk_from_clause(search, statement->update_stmt->from_clause, statement->update_stmt->n_from_clause) :
				0;
		case PG_QUERY__NODE__NODE_DELETE_STMT:
			return statement->delete_stmt != NULL ?
				sqlparser_view_readonly_clause_walk_from_clause(search, statement->delete_stmt->using_clause, statement->delete_stmt->n_using_clause) :
				0;
		case PG_QUERY__NODE__NODE_MERGE_STMT:
			return sqlparser_view_readonly_clause_walk_merge(search, statement->merge_stmt);
		case PG_QUERY__NODE__NODE_VIEW_STMT:
			return statement->view_stmt != NULL &&
					statement->view_stmt->query != NULL &&
					statement->view_stmt->query->node_case == PG_QUERY__NODE__NODE_SELECT_STMT ?
				sqlparser_view_readonly_clause_walk_select(search, statement->view_stmt->query->select_stmt) :
				0;
		case PG_QUERY__NODE__NODE_CREATE_TABLE_AS_STMT:
			return statement->create_table_as_stmt != NULL &&
					statement->create_table_as_stmt->query != NULL &&
					statement->create_table_as_stmt->query->node_case == PG_QUERY__NODE__NODE_SELECT_STMT ?
				sqlparser_view_readonly_clause_walk_select(search, statement->create_table_as_stmt->query->select_stmt) :
				0;
		default:
			return 0;
	}
}

static sqlparser_status_t sqlparser_view_readonly_clause_search(
	sqlparser_handle_t *handle,
	size_t statement_index,
	int want_target,
	size_t target_index,
	sqlparser_view_readonly_clause_search_t *out_search,
	sqlparser_error_t *out_error)
{
	PgQuery__Node *statement;
	sqlparser_status_t status;

	if (handle == NULL || out_search == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "readonly clause search requires non-NULL arguments");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	statement = NULL;
	status = sqlparser_get_statement_node(handle, statement_index, &statement, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	memset(out_search, 0, sizeof(*out_search));
	out_search->handle = handle;
	out_search->statement_index = statement_index;
	out_search->want_target = want_target;
	out_search->target_index = target_index;
	out_search->target_kind = SQLPARSER_CLAUSE_KIND_UNKNOWN;
	(void)sqlparser_view_readonly_clause_walk_statement(out_search, statement);
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_view_full_clause_sql(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	size_t clause_index,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	sqlparser_view_readonly_clause_search_t readonly;
	size_t base_count;
	char *core_sql;
	sqlparser_status_t status;

	if (out_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "out_sql must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_sql = NULL;
	base_count = 0U;
	status = sqlparser_statement_clause_count(handle, statement_index, &base_count, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	if (clause_index < base_count) {
		return sqlparser_statement_clause_sql(handle, statement_index, clause_index, out_sql, out_error);
	}
	status = sqlparser_view_readonly_clause_search(
		(sqlparser_handle_t *)handle,
		statement_index,
		1,
		clause_index - base_count,
		&readonly,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	if (readonly.target_expr == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "clause_index is out of range");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	core_sql = NULL;
	status = sqlparser_render_update_assignment_node_sql(
		handle,
		readonly.target_expr,
		&core_sql,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	status = sqlparser_postprocess_handle_sql_fragment(
		handle,
		statement_index,
		core_sql,
		SQLPARSER_FRAGMENT_CONTEXT_EXPRESSION,
		(ProtobufCMessage *const *)&readonly.target_expr,
		1U,
		"view clause",
		out_sql,
		out_error);
	free(core_sql);
	return status;
}

static const char *sqlparser_transaction_keyword(const PgQuery__TransactionStmt *stmt)
{
	if (stmt == NULL) {
		return "transaction";
	}
	switch (stmt->kind) {
		case PG_QUERY__TRANSACTION_STMT_KIND__TRANS_STMT_BEGIN:
		case PG_QUERY__TRANSACTION_STMT_KIND__TRANS_STMT_START:
			return "begin";
		case PG_QUERY__TRANSACTION_STMT_KIND__TRANS_STMT_COMMIT:
			return "commit";
		case PG_QUERY__TRANSACTION_STMT_KIND__TRANS_STMT_ROLLBACK:
			return "rollback";
		case PG_QUERY__TRANSACTION_STMT_KIND__TRANS_STMT_SAVEPOINT:
			return "savepoint";
		case PG_QUERY__TRANSACTION_STMT_KIND__TRANS_STMT_RELEASE:
			return "release";
		case PG_QUERY__TRANSACTION_STMT_KIND__TRANS_STMT_ROLLBACK_TO:
			return "rollback_to";
		case PG_QUERY__TRANSACTION_STMT_KIND__TRANS_STMT_PREPARE:
			return "prepare_transaction";
		case PG_QUERY__TRANSACTION_STMT_KIND__TRANS_STMT_COMMIT_PREPARED:
			return "commit_prepared";
		case PG_QUERY__TRANSACTION_STMT_KIND__TRANS_STMT_ROLLBACK_PREPARED:
			return "rollback_prepared";
		default:
			return "transaction";
	}
}

static const char *sqlparser_drop_keyword(const PgQuery__DropStmt *stmt)
{
	if (stmt == NULL) {
		return "drop";
	}
	switch (stmt->remove_type) {
		case PG_QUERY__OBJECT_TYPE__OBJECT_VIEW:
			return "drop_view";
		case PG_QUERY__OBJECT_TYPE__OBJECT_TABLE:
			return "drop_table";
		case PG_QUERY__OBJECT_TYPE__OBJECT_DATABASE:
			return "drop_database";
		case PG_QUERY__OBJECT_TYPE__OBJECT_SCHEMA:
			return "drop_schema";
		case PG_QUERY__OBJECT_TYPE__OBJECT_INDEX:
			return "drop_index";
		case PG_QUERY__OBJECT_TYPE__OBJECT_SEQUENCE:
			return "drop_sequence";
		default:
			return "drop";
	}
}

static const char *sqlparser_vacuum_keyword(const PgQuery__VacuumStmt *stmt)
{
	return stmt != NULL && stmt->is_vacuumcmd ? "vacuum" : "analyze";
}

static const char *sqlparser_statement_keyword_from_node(const PgQuery__Node *statement)
{
	if (statement == NULL) {
		return "unknown";
	}
	switch (statement->node_case) {
		case PG_QUERY__NODE__NODE_SELECT_STMT:
			return "select";
		case PG_QUERY__NODE__NODE_INSERT_STMT:
			return "insert";
		case PG_QUERY__NODE__NODE_UPDATE_STMT:
			return "update";
		case PG_QUERY__NODE__NODE_DELETE_STMT:
			return "delete";
		case PG_QUERY__NODE__NODE_MERGE_STMT:
			return "merge";
		case PG_QUERY__NODE__NODE_VIEW_STMT:
			return "create_view";
		case PG_QUERY__NODE__NODE_CREATE_STMT:
			return "create_table";
		case PG_QUERY__NODE__NODE_CREATE_TABLE_AS_STMT:
			return statement->create_table_as_stmt != NULL &&
				statement->create_table_as_stmt->objtype == PG_QUERY__OBJECT_TYPE__OBJECT_MATVIEW ?
				"create_materialized_view" :
				"create_table_as";
		case PG_QUERY__NODE__NODE_CREATE_SCHEMA_STMT:
			return "create_schema";
		case PG_QUERY__NODE__NODE_CREATE_SEQ_STMT:
			return "create_sequence";
		case PG_QUERY__NODE__NODE_ALTER_SEQ_STMT:
			return "alter_sequence";
		case PG_QUERY__NODE__NODE_INDEX_STMT:
			return "create_index";
		case PG_QUERY__NODE__NODE_DROP_STMT:
			return sqlparser_drop_keyword(statement->drop_stmt);
		case PG_QUERY__NODE__NODE_ALTER_TABLE_STMT:
			return "alter_table";
		case PG_QUERY__NODE__NODE_RENAME_STMT:
			return "rename";
		case PG_QUERY__NODE__NODE_GRANT_STMT:
			return statement->grant_stmt != NULL && !statement->grant_stmt->is_grant ? "revoke" : "grant";
		case PG_QUERY__NODE__NODE_VACUUM_STMT:
			return sqlparser_vacuum_keyword(statement->vacuum_stmt);
		case PG_QUERY__NODE__NODE_TRANSACTION_STMT:
			return sqlparser_transaction_keyword(statement->transaction_stmt);
		case PG_QUERY__NODE__NODE_VARIABLE_SET_STMT:
			return statement->variable_set_stmt != NULL &&
				(statement->variable_set_stmt->kind == PG_QUERY__VARIABLE_SET_KIND__VAR_RESET ||
				 statement->variable_set_stmt->kind == PG_QUERY__VARIABLE_SET_KIND__VAR_RESET_ALL) ?
				"reset" :
				"set";
		case PG_QUERY__NODE__NODE_DISCARD_STMT:
			return "discard";
		case PG_QUERY__NODE__NODE_CONSTRAINTS_SET_STMT:
			return "set";
		case PG_QUERY__NODE__NODE_PREPARE_STMT:
			return "prepare";
		case PG_QUERY__NODE__NODE_EXECUTE_STMT:
			return "execute";
		case PG_QUERY__NODE__NODE_DEALLOCATE_STMT:
			return "deallocate";
		default:
			return sqlparser_statement_kind_name(sqlparser_statement_kind_from_case(statement->node_case));
	}
}

static int sqlparser_variable_set_name_is(const PgQuery__VariableSetStmt *stmt, const char *name)
{
	return stmt != NULL &&
		stmt->name != NULL &&
		name != NULL &&
		strcmp(stmt->name, name) == 0;
}

static int sqlparser_variable_set_name_has_prefix(const PgQuery__VariableSetStmt *stmt, const char *prefix)
{
	size_t prefix_len;

	if (stmt == NULL || stmt->name == NULL || prefix == NULL) {
		return 0;
	}
	prefix_len = strlen(prefix);
	return strncmp(stmt->name, prefix, prefix_len) == 0;
}

static int sqlparser_variable_set_name_is_internal(const PgQuery__VariableSetStmt *stmt)
{
	static const char *const names[] = {
		SQLPARSER_INTERNAL_CURRENT_DATABASE,
		SQLPARSER_INTERNAL_CURRENT_SCHEMA,
		SQLPARSER_INTERNAL_MYSQL_PREPARE,
		SQLPARSER_INTERNAL_MYSQL_EXECUTE,
		SQLPARSER_INTERNAL_MYSQL_DEALLOCATE_PREPARE,
		SQLPARSER_INTERNAL_MYSQL_DROP_PREPARE,
		SQLPARSER_INTERNAL_MYSQL_SESSION_STATEMENT,
		SQLPARSER_INTERNAL_SQLSERVER_SP_PREPARE,
		SQLPARSER_INTERNAL_SQLSERVER_SP_EXECUTE,
		SQLPARSER_INTERNAL_SQLSERVER_SP_PREPEXEC,
		SQLPARSER_INTERNAL_SQLSERVER_SP_UNPREPARE,
		SQLPARSER_INTERNAL_SQLSERVER_SP_EXECUTESQL,
		SQLPARSER_INTERNAL_SQLSERVER_CREATE_APPLICATION_ROLE,
		SQLPARSER_INTERNAL_SQLSERVER_ALTER_APPLICATION_ROLE,
		SQLPARSER_INTERNAL_SQLSERVER_DROP_APPLICATION_ROLE,
		SQLPARSER_INTERNAL_SQLSERVER_CREATE_SYNONYM,
		SQLPARSER_INTERNAL_SQLSERVER_DROP_SYNONYM,
		SQLPARSER_INTERNAL_SQLSERVER_CREATE_TYPE,
		SQLPARSER_INTERNAL_SQLSERVER_ALTER_DATABASE,
		SQLPARSER_INTERNAL_SQLSERVER_DROP_INDEX,
		SQLPARSER_INTERNAL_SQLSERVER_UPDATE_STATISTICS,
		SQLPARSER_INTERNAL_SQLSERVER_SET_STATEMENT,
		SQLPARSER_INTERNAL_SQLSERVER_EXECUTE_STATEMENT,
		SQLPARSER_INTERNAL_SQLSERVER_REVERT_STATEMENT,
		SQLPARSER_INTERNAL_SQLSERVER_SETUSER_STATEMENT,
		SQLPARSER_INTERNAL_SQLSERVER_CREATE_USER,
		SQLPARSER_INTERNAL_SQLSERVER_ALTER_USER,
		SQLPARSER_INTERNAL_SQLSERVER_CREATE_ROLE,
		SQLPARSER_INTERNAL_SQLSERVER_ALTER_ROLE,
		SQLPARSER_INTERNAL_SQLSERVER_ALTER_SCHEMA,
		SQLPARSER_INTERNAL_SQLSERVER_ALTER_AUTHORIZATION,
		SQLPARSER_INTERNAL_ORACLE_EXECUTE_IMMEDIATE,
		SQLPARSER_INTERNAL_ORACLE_CREATE_SYNONYM,
		SQLPARSER_INTERNAL_ORACLE_DROP_SYNONYM,
		SQLPARSER_INTERNAL_ORACLE_EXPLAIN_PLAN,
		SQLPARSER_INTERNAL_ORACLE_SESSION_STATEMENT,
		SQLPARSER_INTERNAL_DAMENG_SESSION_STATEMENT,
		SQLPARSER_INTERNAL_DAMENG_EXEC_SQL_PREPARE,
		SQLPARSER_INTERNAL_DAMENG_EXEC_SQL_EXECUTE,
		SQLPARSER_INTERNAL_DAMENG_EXEC_SQL_DEALLOCATE_PREPARE,
		SQLPARSER_INTERNAL_VASTBASE_SESSION_STATEMENT
	};
	size_t index;

	if (stmt == NULL || stmt->name == NULL) {
		return 0;
	}
	if (sqlparser_variable_set_name_has_prefix(
		    stmt, SQLPARSER_INTERNAL_ORACLE_SESSION_PARAM_PREFIX) ||
	    sqlparser_variable_set_name_has_prefix(
		    stmt, SQLPARSER_INTERNAL_DAMENG_SESSION_PARAM_PREFIX)) {
		return 1;
	}
	for (index = 0U; index < sizeof(names) / sizeof(names[0]); index++) {
		if (strcmp(stmt->name, names[index]) == 0) {
			return 1;
		}
	}
	return 0;
}

static int sqlparser_variable_set_is_prepared_statement(const PgQuery__VariableSetStmt *stmt)
{
	return sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_MYSQL_PREPARE) ||
		sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_MYSQL_EXECUTE) ||
		sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_MYSQL_DEALLOCATE_PREPARE) ||
		sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_MYSQL_DROP_PREPARE) ||
		sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_SQLSERVER_SP_PREPARE) ||
		sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_SQLSERVER_SP_EXECUTE) ||
		sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_SQLSERVER_SP_PREPEXEC) ||
		sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_SQLSERVER_SP_UNPREPARE) ||
		sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_SQLSERVER_SP_EXECUTESQL) ||
		sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_ORACLE_EXECUTE_IMMEDIATE) ||
		sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_DAMENG_EXEC_SQL_PREPARE) ||
		sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_DAMENG_EXEC_SQL_EXECUTE) ||
		sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_DAMENG_EXEC_SQL_DEALLOCATE_PREPARE);
}

static int sqlparser_view_variable_set_is_internal_rewrite(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	const PgQuery__VariableSetStmt *stmt);

static const char *sqlparser_variable_set_column_keyword(
	const sqlparser_handle_t *handle,
	const PgQuery__VariableSetStmt *stmt)
{
	if (sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_CURRENT_DATABASE) &&
	    handle != NULL &&
	    (sqlparser_dialect_is_mysql_compatible(handle->dialect) ||
	     sqlparser_dialect_is_sqlserver_compatible(handle->dialect))) {
		return "use";
	}
	if (sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_MYSQL_PREPARE) ||
	    sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_DAMENG_EXEC_SQL_PREPARE)) {
		return "prepare";
	}
	if (sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_MYSQL_SESSION_STATEMENT)) {
		return "set";
	}
	if (sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_MYSQL_EXECUTE) ||
	    sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_ORACLE_EXECUTE_IMMEDIATE) ||
	    sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_DAMENG_EXEC_SQL_EXECUTE)) {
		return "execute";
	}
	if (sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_MYSQL_DEALLOCATE_PREPARE) ||
	    sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_MYSQL_DROP_PREPARE) ||
	    sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_DAMENG_EXEC_SQL_DEALLOCATE_PREPARE)) {
		return "deallocate";
	}
	if (sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_SQLSERVER_SP_PREPARE)) {
		return "sp_prepare";
	}
	if (sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_SQLSERVER_SP_EXECUTE)) {
		return "sp_execute";
	}
	if (sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_SQLSERVER_SP_PREPEXEC)) {
		return "sp_prepexec";
	}
	if (sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_SQLSERVER_SP_UNPREPARE)) {
		return "sp_unprepare";
	}
	if (sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_SQLSERVER_SP_EXECUTESQL)) {
		return "sp_executesql";
	}
	if (sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_SQLSERVER_EXECUTE_STATEMENT)) {
		return "execute";
	}
	if (sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_SQLSERVER_REVERT_STATEMENT)) {
		return "revert";
	}
	if (sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_SQLSERVER_SETUSER_STATEMENT)) {
		return "setuser";
	}
	if (sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_SQLSERVER_CREATE_APPLICATION_ROLE)) {
		return "create_application_role";
	}
	if (sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_SQLSERVER_ALTER_APPLICATION_ROLE)) {
		return "alter_application_role";
	}
	if (sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_SQLSERVER_DROP_APPLICATION_ROLE)) {
		return "drop_application_role";
	}
	if (sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_SQLSERVER_CREATE_SYNONYM)) {
		return "create_synonym";
	}
	if (sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_SQLSERVER_DROP_SYNONYM)) {
		return "drop_synonym";
	}
	if (sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_SQLSERVER_CREATE_TYPE)) {
		return "create_type";
	}
	if (sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_SQLSERVER_ALTER_DATABASE)) {
		return "alter_database";
	}
	if (sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_SQLSERVER_DROP_INDEX)) {
		return "drop_index";
	}
	if (sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_SQLSERVER_UPDATE_STATISTICS)) {
		return "update_statistics";
	}
	if (sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_SQLSERVER_SET_STATEMENT)) {
		return "set";
	}
	if (sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_SQLSERVER_CREATE_USER)) {
		return "create_user";
	}
	if (sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_SQLSERVER_ALTER_USER)) {
		return "alter_user";
	}
	if (sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_SQLSERVER_CREATE_ROLE)) {
		return "create_role";
	}
	if (sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_SQLSERVER_ALTER_ROLE)) {
		return "alter_role";
	}
	if (sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_SQLSERVER_ALTER_SCHEMA)) {
		return "alter_schema";
	}
	if (sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_SQLSERVER_ALTER_AUTHORIZATION)) {
		return "alter_authorization";
	}
	if (sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_ORACLE_CREATE_SYNONYM)) {
		return "create_synonym";
	}
	if (sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_ORACLE_DROP_SYNONYM)) {
		return "drop_synonym";
	}
	if (sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_ORACLE_EXPLAIN_PLAN)) {
		return "explain_plan";
	}
	if (sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_ORACLE_SESSION_STATEMENT) ||
	    sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_DAMENG_SESSION_STATEMENT) ||
	    sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_VASTBASE_SESSION_STATEMENT)) {
		return "alter_session";
	}
	return "set";
}

static const char *sqlparser_statement_keyword_for_handle(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	const PgQuery__Node *statement)
{
	PgQuery__VariableSetStmt *stmt;
	const char *dialect_keyword;
	int is_internal_rewrite;

	if (sqlparser_control_unit_is_condition(handle, statement_index)) {
		return "condition";
	}

	if (handle != NULL &&
	    handle->dialect_ops != NULL &&
	    handle->dialect_ops->statement_keyword != NULL) {
		dialect_keyword = handle->dialect_ops->statement_keyword(
			handle->dialect_state,
			statement_index,
			statement);
		if (dialect_keyword != NULL) {
			return dialect_keyword;
		}
	}

	if (statement == NULL ||
	    statement->node_case != PG_QUERY__NODE__NODE_VARIABLE_SET_STMT ||
	    statement->variable_set_stmt == NULL) {
		return sqlparser_statement_keyword_from_node(statement);
	}

	stmt = statement->variable_set_stmt;
	if (stmt->kind == PG_QUERY__VARIABLE_SET_KIND__VAR_RESET ||
	    stmt->kind == PG_QUERY__VARIABLE_SET_KIND__VAR_RESET_ALL) {
		return "reset";
	}
	is_internal_rewrite = sqlparser_variable_set_name_is_internal(stmt) ?
		sqlparser_view_variable_set_is_internal_rewrite(
			handle,
			statement_index,
			stmt) :
		0;
	if (sqlparser_variable_set_name_is_internal(stmt) &&
	    !is_internal_rewrite) {
		return "set";
	}
	if (sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_CURRENT_DATABASE)) {
		if (handle != NULL && sqlparser_dialect_is_oracle_compatible(handle->dialect)) {
			return "alter_session";
		}
		if (handle != NULL &&
		    (sqlparser_dialect_is_mysql_compatible(handle->dialect) ||
		     sqlparser_dialect_is_sqlserver_compatible(handle->dialect))) {
			return "use";
		}
	}
	if (sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_CURRENT_SCHEMA) &&
	    handle != NULL &&
	    sqlparser_dialect_is_oracle_or_dameng_compatible(handle->dialect)) {
		return "alter_session";
	}
	if (handle != NULL &&
	    sqlparser_dialect_is_oracle_compatible(handle->dialect) &&
	    sqlparser_variable_set_name_has_prefix(stmt, SQLPARSER_INTERNAL_ORACLE_SESSION_PARAM_PREFIX)) {
		return "alter_session";
	}
	if (sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_ORACLE_SESSION_STATEMENT) ||
	    sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_DAMENG_SESSION_STATEMENT) ||
	    sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_VASTBASE_SESSION_STATEMENT)) {
		return "alter_session";
	}
	if (sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_MYSQL_SESSION_STATEMENT) ||
	    sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_SQLSERVER_EXECUTE_STATEMENT) ||
	    sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_SQLSERVER_REVERT_STATEMENT) ||
	    sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_SQLSERVER_SETUSER_STATEMENT)) {
		return sqlparser_variable_set_column_keyword(handle, stmt);
	}
	if (handle != NULL &&
	    sqlparser_dialect_is_oracle_compatible(handle->dialect) &&
	    (sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_ORACLE_CREATE_SYNONYM) ||
	     sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_ORACLE_DROP_SYNONYM) ||
	     sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_ORACLE_EXPLAIN_PLAN))) {
		return sqlparser_variable_set_column_keyword(handle, stmt);
	}
	if (handle != NULL &&
	    handle->dialect == SQLPARSER_DIALECT_DAMENG &&
	    sqlparser_variable_set_name_has_prefix(stmt, SQLPARSER_INTERNAL_DAMENG_SESSION_PARAM_PREFIX)) {
		return "alter_session";
	}
	if (sqlparser_variable_set_is_prepared_statement(stmt)) {
		return sqlparser_variable_set_column_keyword(handle, stmt);
	}
	if (handle != NULL && sqlparser_dialect_is_sqlserver_compatible(handle->dialect) &&
	    (sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_SQLSERVER_CREATE_APPLICATION_ROLE) ||
	     sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_SQLSERVER_ALTER_APPLICATION_ROLE) ||
	     sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_SQLSERVER_DROP_APPLICATION_ROLE) ||
	     sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_SQLSERVER_CREATE_SYNONYM) ||
	     sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_SQLSERVER_DROP_SYNONYM) ||
	     sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_SQLSERVER_CREATE_TYPE) ||
	     sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_SQLSERVER_ALTER_DATABASE) ||
	     sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_SQLSERVER_DROP_INDEX) ||
	     sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_SQLSERVER_UPDATE_STATISTICS) ||
	     sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_SQLSERVER_SET_STATEMENT) ||
	     sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_SQLSERVER_CREATE_USER) ||
	     sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_SQLSERVER_ALTER_USER) ||
	     sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_SQLSERVER_CREATE_ROLE) ||
	     sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_SQLSERVER_ALTER_ROLE) ||
	     sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_SQLSERVER_ALTER_SCHEMA) ||
	     sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_SQLSERVER_ALTER_AUTHORIZATION))) {
		return sqlparser_variable_set_column_keyword(handle, stmt);
	}
	return "set";
}

static const char *sqlparser_view_func_call_name(const PgQuery__FuncCall *func_call)
{
	const char *name;
	size_t index;

	if (func_call == NULL || func_call->n_funcname == 0U) {
		return NULL;
	}
	name = NULL;
	for (index = func_call->n_funcname; index > 0U; index--) {
		if (sqlparser_node_string_value(func_call->funcname[index - 1U], &name)) {
			break;
		}
	}
	return name;
}

static char *sqlparser_view_func_call_name_dup(const PgQuery__FuncCall *func_call)
{
	return sqlparser_view_ascii_upper_dup(sqlparser_view_func_call_name(func_call));
}

static int sqlparser_view_func_call_is_name(
	const PgQuery__FuncCall *func_call,
	const char *expected_name)
{
	const char *name;

	if (expected_name == NULL) {
		return 0;
	}
	name = sqlparser_view_func_call_name(func_call);
	return name != NULL && sqlparser_text_equal_ci(name, expected_name);
}

static int sqlparser_view_func_call_is_mysql_join_on(const PgQuery__FuncCall *func_call)
{
	return sqlparser_view_func_call_is_name(func_call, SQLPARSER_INTERNAL_MYSQL_JOIN_ON) ||
		sqlparser_view_func_call_is_name(func_call, SQLPARSER_INTERNAL_MYSQL_LEFT_JOIN_ON) ||
		sqlparser_view_func_call_is_name(func_call, SQLPARSER_INTERNAL_MYSQL_RIGHT_JOIN_ON) ||
		sqlparser_view_func_call_is_name(func_call, SQLPARSER_INTERNAL_MYSQL_CROSS_JOIN_ON);
}

static int sqlparser_view_func_call_is_dameng_update_join_on(
	const PgQuery__FuncCall *func_call)
{
	return sqlparser_view_func_call_is_name(
		func_call,
		SQLPARSER_INTERNAL_DAMENG_UPDATE_JOIN_ON);
}

static const char *sqlparser_view_bool_expr_name(const PgQuery__BoolExpr *expr)
{
	if (expr == NULL) {
		return NULL;
	}
	switch (expr->boolop) {
		case PG_QUERY__BOOL_EXPR_TYPE__AND_EXPR:
			return "AND";
		case PG_QUERY__BOOL_EXPR_TYPE__OR_EXPR:
			return "OR";
		case PG_QUERY__BOOL_EXPR_TYPE__NOT_EXPR:
			return "NOT";
		default:
			return NULL;
	}
}

static const char *sqlparser_view_min_max_name(const PgQuery__MinMaxExpr *expr)
{
	if (expr == NULL) {
		return NULL;
	}
	switch (expr->op) {
		case PG_QUERY__MIN_MAX_OP__IS_GREATEST:
			return "GREATEST";
		case PG_QUERY__MIN_MAX_OP__IS_LEAST:
			return "LEAST";
		default:
			return NULL;
	}
}

static size_t sqlparser_view_find_name_selector_index(
	sqlparser_view_build_t *build,
	char **slot)
{
	size_t index;
	sqlparser_error_t error;

	memset(&error, 0, sizeof(error));
	if (slot == NULL ||
	    sqlparser_find_statement_name_index_by_slot(
		    build->handle,
		    build->statement_index,
		    slot,
		    &index,
		    &error) != SQLPARSER_STATUS_OK) {
		return (size_t)-1;
	}
	return index;
}

static size_t sqlparser_view_find_value_index(
	sqlparser_view_build_t *build,
	PgQuery__Node *value_node)
{
	size_t index;
	sqlparser_error_t error;

	memset(&error, 0, sizeof(error));
	if (value_node == NULL ||
	    sqlparser_find_statement_node_index_by_node(
		    build->handle,
		    build->statement_index,
		    value_node,
		    &index,
		    &error) != SQLPARSER_STATUS_OK) {
		return (size_t)-1;
	}
	return index;
}

static int sqlparser_view_dialect_uses_at_binds(sqlparser_dialect_t dialect)
{
	return dialect == SQLPARSER_DIALECT_SQLSERVER ||
	       dialect == SQLPARSER_DIALECT_VASTBASE_SQLSERVER;
}

static void sqlparser_view_bind_info_release(sqlparser_view_bind_info_t *info)
{
	if (info == NULL) {
		return;
	}
	free(info->name);
	info->name = NULL;
	info->kind = SQLPARSER_BIND_KIND_NONE;
	info->position = 0U;
}

static int sqlparser_view_public_word_at(
	const char *sql,
	size_t pos,
	size_t end,
	const char *word)
{
	size_t index;
	size_t length;

	if (sql == NULL || word == NULL || pos >= end) {
		return 0;
	}
	length = strlen(word);
	if (length > end - pos ||
	    (pos > 0U &&
	     sqlparser_public_char_is_ident(
		     (unsigned char)sql[pos - 1U])) ||
	    (pos + length < end &&
	     sqlparser_public_char_is_ident(
		     (unsigned char)sql[pos + length]))) {
		return 0;
	}
	for (index = 0U; index < length; index++) {
		if (tolower((unsigned char)sql[pos + index]) !=
		    tolower((unsigned char)word[index])) {
			return 0;
		}
	}
	return 1;
}

static int sqlparser_view_public_statement_span_in_sql(
	const sqlparser_handle_t *handle,
	const char *sql,
	int use_current_control_offsets,
	size_t statement_index,
	size_t *out_start,
	size_t *out_end)
{
	size_t after_go;
	size_t current;
	size_t end;
	size_t pos;
	size_t skipped;
	size_t start;

	if (handle == NULL || sql == NULL || out_start == NULL ||
	    out_end == NULL) {
		return 0;
	}
	if (handle->control != NULL) {
		const sqlparser_control_unit_t *unit;

		if (statement_index >= handle->control->unit_count) {
			return 0;
		}
		unit = &handle->control->units[statement_index];
		if (use_current_control_offsets) {
			start = unit->current_offset;
			if (unit->current_length > (size_t)-1 - start) {
				return 0;
			}
			end = start + unit->current_length;
		} else {
			if (unit->source_offset > handle->sql_len ||
			    unit->source_length > handle->sql_len - unit->source_offset) {
				return 0;
			}
			start = unit->source_offset;
			end = start + unit->source_length;
		}
		start = sqlparser_public_skip_trivia(
			handle->dialect,
			sql,
			start);
		while (end > start && isspace((unsigned char)sql[end - 1U])) {
			end--;
		}
		*out_start = start;
		*out_end = end;
		return start < end;
	}

	current = 0U;
	pos = 0U;
	while (sql[pos] != '\0') {
		pos = sqlparser_public_skip_trivia(
			handle->dialect,
			sql,
			pos);
		for (;;) {
			if (sql[pos] == ';') {
				pos++;
			} else if (sqlparser_public_sqlserver_go_at(
					   handle->dialect,
					   sql,
					   pos,
					   &after_go)) {
				pos = after_go;
			} else {
				break;
			}
			pos = sqlparser_public_skip_trivia(
				handle->dialect,
				sql,
				pos);
		}
		if (sql[pos] == '\0') {
			break;
		}
		start = pos;
		while (sql[pos] != '\0' && sql[pos] != ';') {
			if (sqlparser_public_sqlserver_go_at(
				    handle->dialect,
				    sql,
				    pos,
				    &after_go)) {
				break;
			}
			skipped = sqlparser_public_skip_quoted_or_comment(
				handle->dialect,
				sql,
				pos);
			pos = skipped != pos ? skipped : pos + 1U;
		}
		end = pos;
		while (end > start && isspace((unsigned char)sql[end - 1U])) {
			end--;
		}
		if (start < end) {
			if (current == statement_index) {
				*out_start = start;
				*out_end = end;
				return 1;
			}
			current++;
		}
		if (sql[pos] == ';') {
			pos++;
		} else if (sqlparser_public_sqlserver_go_at(
				   handle->dialect,
				   sql,
				   pos,
				   &after_go)) {
			pos = after_go;
		}
	}
	return 0;
}

static int sqlparser_view_public_statement_span(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	size_t *out_start,
	size_t *out_end)
{
	const char *sql;

	if (handle == NULL) {
		return 0;
	}
	sql = sqlparser_effective_sql(handle);
	return sqlparser_view_public_statement_span_in_sql(
		handle,
		sql,
		handle->current_sql != NULL,
		statement_index,
		out_start,
		out_end);
}

static int sqlparser_view_insert_values_cell_span(
	const sqlparser_handle_t *handle,
	const char *sql,
	size_t values_start,
	size_t statement_end,
	size_t row_index,
	size_t column_index,
	size_t *out_start,
	size_t *out_end,
	sqlparser_error_t *out_error)
{
	size_t brace_depth;
	size_t bracket_depth;
	size_t cell_column;
	size_t cell_start;
	size_t paren_depth;
	size_t pos;
	size_t row;
	size_t skipped;

	pos = sqlparser_public_skip_trivia(
		handle->dialect,
		sql,
		values_start);
	if (pos >= statement_end || sql[pos] != '(') {
		return 0;
	}
	row = 0U;
	for (;;) {
		pos++;
		cell_column = 0U;
		cell_start = sqlparser_public_skip_space(sql, pos);
		paren_depth = 0U;
		bracket_depth = 0U;
		brace_depth = 0U;
		for (pos = cell_start; pos < statement_end; pos++) {
			size_t cell_end;

			skipped = sqlparser_public_skip_quoted_or_comment(
				handle->dialect,
				sql,
				pos);
			if (skipped != pos) {
				pos = skipped - 1U;
				continue;
			}
			if (sql[pos] == '(') {
				paren_depth++;
				continue;
			}
			if (sql[pos] == '[') {
				bracket_depth++;
				continue;
			}
			if (sql[pos] == '{') {
				brace_depth++;
				continue;
			}
			if (sql[pos] == ')' && paren_depth > 0U) {
				paren_depth--;
				continue;
			}
			if (sql[pos] == ']' && bracket_depth > 0U) {
				bracket_depth--;
				continue;
			}
			if (sql[pos] == '}' && brace_depth > 0U) {
				brace_depth--;
				continue;
			}
			if ((sql[pos] != ',' && sql[pos] != ')') ||
			    paren_depth != 0U ||
			    bracket_depth != 0U ||
			    brace_depth != 0U) {
				continue;
			}
			cell_end = pos;
			while (cell_end > cell_start &&
			       isspace((unsigned char)sql[cell_end - 1U])) {
				cell_end--;
			}
			if (row == row_index && cell_column == column_index) {
				if (cell_end <= cell_start) {
					sqlparser_error_set_message(
						out_error,
						SQLPARSER_STATUS_INTERNAL_ERROR,
						"insert cell source SQL is empty");
					return -1;
				}
				*out_start = cell_start;
				*out_end = cell_end;
				return 1;
			}
			if (sql[pos] == ',') {
				cell_column++;
				cell_start = sqlparser_public_skip_space(
					sql,
					pos + 1U);
				pos = cell_start - 1U;
				continue;
			}
			pos = sqlparser_public_skip_trivia(
				handle->dialect,
				sql,
				pos + 1U);
			if (pos >= statement_end || sql[pos] != ',') {
				return 0;
			}
			pos = sqlparser_public_skip_trivia(
				handle->dialect,
				sql,
				pos + 1U);
			if (pos >= statement_end || sql[pos] != '(') {
				return 0;
			}
			row++;
			break;
		}
		if (pos >= statement_end) {
			return 0;
		}
	}
}

static int sqlparser_view_dml_cell_source_span(
	const sqlparser_handle_t *handle,
	const char *sql,
	size_t start,
	size_t end,
	size_t values_ordinal,
	size_t row_index,
	size_t column_index,
	size_t *out_start,
	size_t *out_end,
	sqlparser_error_t *out_error)
{
	size_t current_values;
	size_t depth;
	size_t pos;
	size_t skipped;

	if (out_start == NULL || out_end == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"insert cell source span must not be NULL");
		return -1;
	}
	if (handle == NULL || sql == NULL || start >= end) {
		return 0;
	}
	current_values = 0U;
	depth = 0U;
	for (pos = start; pos < end; pos++) {
		int status;
		size_t values_content;
		size_t values_end;

		skipped = sqlparser_public_skip_quoted_or_comment(
			handle->dialect,
			sql,
			pos);
		if (skipped != pos) {
			pos = skipped - 1U;
			continue;
		}
		if (sql[pos] == '(') {
			depth++;
			continue;
		}
		if (sql[pos] == ')') {
			if (depth > 0U) {
				depth--;
			}
			continue;
		}
		if (depth != 0U) {
			continue;
		}
		if (sqlparser_view_public_word_at(
			    sql,
			    pos,
			    end,
			    "values")) {
			values_end = pos + strlen("values");
		} else if (sqlparser_dialect_is_mysql_compatible(handle->dialect) &&
			   sqlparser_view_public_word_at(
				   sql,
				   pos,
				   end,
				   "value")) {
			values_end = pos + strlen("value");
		} else {
			continue;
		}
		values_content = sqlparser_public_skip_trivia(
			handle->dialect,
			sql,
			values_end);
		if (values_content >= end || sql[values_content] != '(') {
			continue;
		}
		if (current_values++ != values_ordinal) {
			continue;
		}
		status = sqlparser_view_insert_values_cell_span(
			handle,
			sql,
			values_end,
			end,
			row_index,
			column_index,
			out_start,
			out_end,
			out_error);
		if (status != 0) {
			return status;
		}
	}
	return 0;
}

static int sqlparser_view_multi_insert_cell_source_span(
	const sqlparser_handle_t *handle,
	const sqlparser_dialect_multi_insert_t *multi_insert,
	size_t statement_start,
	size_t statement_end,
	size_t target_branch,
	size_t target_column,
	size_t *out_start,
	size_t *out_end)
{
	const sqlparser_dialect_multi_insert_branch_t *branch;
	const sqlparser_dialect_multi_insert_value_t *value;
	const char *sql;
	size_t brace_depth;
	size_t bracket_depth;
	size_t branch_index;
	size_t cell_end;
	size_t cell_index;
	size_t cell_start;
	size_t paren_depth;
	size_t pos;
	size_t skipped;
	size_t source_length;
	size_t source_start;
	size_t target_end;
	size_t target_start;
	size_t values_close;
	size_t values_open;
	int target_found;

	if (handle == NULL || multi_insert == NULL ||
	    multi_insert->branches == NULL ||
	    target_branch >= multi_insert->branch_count ||
	    multi_insert->source_public_sql == NULL ||
	    statement_start >= statement_end) {
		return 0;
	}
	sql = handle->sql;
	source_length = strlen(multi_insert->source_public_sql);
	if (source_length == 0U ||
	    source_length > statement_end - statement_start) {
		return 0;
	}
	source_start = statement_end - source_length;
	if (source_start <= statement_start ||
	    memcmp(
		    sql + source_start,
		    multi_insert->source_public_sql,
		    source_length) != 0 ||
	    !sqlparser_view_public_word_at(
		    sql,
		    source_start,
		    statement_end,
		    "select")) {
		return 0;
	}

	pos = sqlparser_public_skip_trivia(
		handle->dialect,
		sql,
		statement_start);
	if (!sqlparser_view_public_word_at(
		    sql,
		    pos,
		    source_start,
		    "insert")) {
		return 0;
	}
	pos = sqlparser_public_skip_trivia(
		handle->dialect,
		sql,
		pos + strlen("insert"));
	if (multi_insert->mode == SQLPARSER_DIALECT_MULTI_INSERT_ALL &&
	    sqlparser_view_public_word_at(
		    sql,
		    pos,
		    source_start,
		    "all")) {
		pos += strlen("all");
	} else if (multi_insert->mode ==
			   SQLPARSER_DIALECT_MULTI_INSERT_FIRST &&
		   sqlparser_view_public_word_at(
			   sql,
			   pos,
			   source_start,
			   "first")) {
		pos += strlen("first");
	} else {
		return 0;
	}

	branch_index = 0U;
	paren_depth = 0U;
	target_end = 0U;
	target_found = 0;
	target_start = 0U;
	while (pos < source_start) {
		skipped = sqlparser_public_skip_quoted_or_comment(
			handle->dialect,
			sql,
			pos);
		if (skipped != pos) {
			if (skipped > source_start) {
				return 0;
			}
			pos = skipped;
			continue;
		}
		if (sql[pos] == '(') {
			paren_depth++;
			pos++;
			continue;
		}
		if (sql[pos] == ')') {
			if (paren_depth == 0U) {
				return 0;
			}
			paren_depth--;
			pos++;
			continue;
		}
		if (paren_depth != 0U ||
		    !sqlparser_view_public_word_at(
			    sql,
			    pos,
			    source_start,
			    "values")) {
			pos++;
			continue;
		}
		values_open = sqlparser_public_skip_trivia(
			handle->dialect,
			sql,
			pos + strlen("values"));
		if (values_open >= source_start || sql[values_open] != '(') {
			pos += strlen("values");
			continue;
		}
		if (branch_index >= multi_insert->branch_count ||
		    (branch = &multi_insert->branches[branch_index])->ordinal !=
			    branch_index ||
		    branch->cells == NULL || branch->cell_count == 0U) {
			return 0;
		}

		cell_index = 0U;
		cell_start = sqlparser_public_skip_space(sql, values_open + 1U);
		paren_depth = 0U;
		bracket_depth = 0U;
		brace_depth = 0U;
		values_close = source_start;
		for (pos = cell_start; pos < source_start; pos++) {
			skipped = sqlparser_public_skip_quoted_or_comment(
				handle->dialect,
				sql,
				pos);
			if (skipped != pos) {
				if (skipped > source_start) {
					return 0;
				}
				pos = skipped - 1U;
				continue;
			}
			if (sql[pos] == '(') {
				paren_depth++;
				continue;
			}
			if (sql[pos] == '[') {
				bracket_depth++;
				continue;
			}
			if (sql[pos] == '{') {
				brace_depth++;
				continue;
			}
			if (sql[pos] == ')' && paren_depth > 0U) {
				paren_depth--;
				continue;
			}
			if (sql[pos] == ']' && bracket_depth > 0U) {
				bracket_depth--;
				continue;
			}
			if (sql[pos] == '}' && brace_depth > 0U) {
				brace_depth--;
				continue;
			}
			if ((sql[pos] != ',' && sql[pos] != ')') ||
			    paren_depth != 0U || bracket_depth != 0U ||
			    brace_depth != 0U) {
				continue;
			}
			cell_end = pos;
			while (cell_end > cell_start &&
			       isspace((unsigned char)sql[cell_end - 1U])) {
				cell_end--;
			}
			if (cell_end <= cell_start ||
			    cell_index >= branch->cell_count ||
			    (value = &branch->cells[cell_index])->public_sql == NULL ||
			    cell_end - cell_start != strlen(value->public_sql) ||
			    memcmp(
				    sql + cell_start,
				    value->public_sql,
				    cell_end - cell_start) != 0) {
				return 0;
			}
			if (branch_index == target_branch &&
			    cell_index == target_column) {
				target_start = cell_start;
				target_end = cell_end;
				target_found = 1;
			}
			cell_index++;
			if (sql[pos] == ')') {
				values_close = pos;
				break;
			}
			cell_start = sqlparser_public_skip_space(sql, pos + 1U);
			pos = cell_start - 1U;
		}
		if (values_close == source_start ||
		    cell_index != branch->cell_count) {
			return 0;
		}
		branch_index++;
		paren_depth = 0U;
		pos = values_close + 1U;
	}
	if (paren_depth != 0U || branch_index != multi_insert->branch_count ||
	    !target_found) {
		return 0;
	}
	*out_start = target_start;
	*out_end = target_end;
	return 1;
}

static int sqlparser_view_dml_cell_source_sql(
	const sqlparser_handle_t *handle,
	const char *sql,
	size_t start,
	size_t end,
	size_t values_ordinal,
	size_t row_index,
	size_t column_index,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	size_t cell_end;
	size_t cell_start;
	int status;

	if (out_sql == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"insert cell source output must not be NULL");
		return -1;
	}
	*out_sql = NULL;
	status = sqlparser_view_dml_cell_source_span(
		handle,
		sql,
		start,
		end,
		values_ordinal,
		row_index,
		column_index,
		&cell_start,
		&cell_end,
		out_error);
	if (status <= 0) {
		return status;
	}
	*out_sql = sqlparser_strndup(
		sql + cell_start,
		cell_end - cell_start);
	if (*out_sql == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_NO_MEMORY,
			"out of memory");
		return -1;
	}
	return 1;
}

static int sqlparser_view_statement_dml_cell_source_sql(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	size_t values_ordinal,
	size_t row_index,
	size_t column_index,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	const char *sql;
	size_t end;
	size_t start;

	if (out_sql == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"insert cell source output must not be NULL");
		return -1;
	}
	*out_sql = NULL;
	if (handle == NULL ||
	    (handle->generation != 0UL &&
	     !handle->surface_source_complete)) {
		return 0;
	}
	if (handle->generation != 0UL &&
	    sqlparser_ensure_current_sql_text(handle, out_error) !=
		    SQLPARSER_STATUS_OK) {
		return -1;
	}
	if (!sqlparser_view_public_statement_span(
		    handle,
		    statement_index,
		    &start,
		    &end)) {
		return 0;
	}
	sql = sqlparser_effective_sql(handle);
	return sqlparser_view_dml_cell_source_sql(
		handle,
		sql,
		start,
		end,
		values_ordinal,
		row_index,
		column_index,
		out_sql,
		out_error);
}

int sqlparser_view_insert_cell_source_sql(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	size_t row_index,
	size_t column_index,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	return sqlparser_view_statement_dml_cell_source_sql(
		handle,
		statement_index,
		0U,
		row_index,
		column_index,
		out_sql,
		out_error);
}

static int sqlparser_view_expression_source_span(
	const sqlparser_handle_t *handle,
	const sqlparser_identifier_origin_map_t **origins,
	sqlparser_view_expression_source_cache_t *cache,
	PgQuery__Node *value_node,
	const sqlparser_surface_source_edits_t *surface_edits,
	size_t *out_start,
	size_t *out_end,
	sqlparser_error_t *out_error);

int sqlparser_view_insert_cell_source_span(
	sqlparser_handle_t *handle,
	const sqlparser_surface_source_edits_t *surface_edits,
	sqlparser_view_expression_source_cache_t *cache,
	int allow_comments,
	size_t statement_index,
	size_t row_index,
	size_t column_index,
	size_t *out_start,
	size_t *out_end,
	sqlparser_error_t *out_error)
{
	const sqlparser_dialect_multi_insert_t *multi_insert;
	const sqlparser_identifier_origin_map_t *origins;
	PgQuery__InsertStmt *insert_stmt;
	PgQuery__Node *row_node;
	PgQuery__Node *statement;
	PgQuery__Node *value_node;
	PgQuery__SelectStmt *values_stmt;
	sqlparser_graph_insert_mode_t insert_mode;
	sqlparser_status_t status;
	size_t left_delimiter;
	size_t pos;
	size_t right_delimiter;
	size_t skipped;
	size_t statement_end;
	size_t statement_start;
	int source_status;

	if (out_start == NULL || out_end == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"insert cell source span must not be NULL");
		return -1;
	}
	*out_start = 0U;
	*out_end = 0U;
	if (handle == NULL || handle->sql == NULL) {
		return 0;
	}
	multi_insert = sqlparser_dialect_is_oracle_compatible(handle->dialect) ?
		sqlparser_dialect_state_multi_insert(
			handle->dialect,
			handle->dialect_state) : NULL;
	if (multi_insert != NULL) {
		if (statement_index != 0U ||
		    !sqlparser_view_public_statement_span_in_sql(
			    handle,
			    handle->sql,
			    0,
			    statement_index,
			    &statement_start,
			    &statement_end)) {
			return 0;
		}
		source_status = sqlparser_view_multi_insert_cell_source_span(
			handle,
			multi_insert,
			statement_start,
			statement_end,
			row_index,
			column_index,
			out_start,
			out_end);
		if (source_status <= 0) {
			return source_status;
		}
	} else {
		status = sqlparser_get_statement_node(
			handle,
			statement_index,
			&statement,
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			return -1;
		}
		if (statement->node_case != PG_QUERY__NODE__NODE_INSERT_STMT ||
		    (insert_stmt = statement->insert_stmt) == NULL ||
		    sqlparser_insert_source_from_stmt(insert_stmt) !=
			    SQLPARSER_INSERT_SOURCE_VALUES ||
		    insert_stmt->select_stmt == NULL ||
		    (values_stmt = insert_stmt->select_stmt->select_stmt) == NULL) {
			return 0;
		}
		insert_mode = SQLPARSER_GRAPH_INSERT_MODE_VALUES;
		if (handle->dialect_ops != NULL &&
		    handle->dialect_ops->insert_mode != NULL) {
			insert_mode = handle->dialect_ops->insert_mode(
				handle->dialect_state,
				statement_index,
				insert_mode);
		}
		if (insert_mode != SQLPARSER_GRAPH_INSERT_MODE_VALUES &&
		    insert_mode != SQLPARSER_GRAPH_INSERT_MODE_REPLACE_VALUES) {
			return 0;
		}
		if (values_stmt->values_lists == NULL ||
		    row_index >= values_stmt->n_values_lists ||
		    (row_node = values_stmt->values_lists[row_index]) == NULL ||
		    row_node->node_case != PG_QUERY__NODE__NODE_LIST ||
		    row_node->list == NULL || row_node->list->items == NULL ||
		    column_index >= row_node->list->n_items ||
		    (value_node = row_node->list->items[column_index]) == NULL) {
			return 0;
		}
		origins = NULL;
		source_status = sqlparser_view_expression_source_span(
			handle,
			&origins,
			cache,
			value_node,
			surface_edits,
			out_start,
			out_end,
			out_error);
		if (source_status <= 0) {
			return source_status;
		}
		if (!sqlparser_view_public_statement_span_in_sql(
			    handle,
			    handle->sql,
			    0,
			    statement_index,
			    &statement_start,
			    &statement_end)) {
			*out_start = 0U;
			*out_end = 0U;
			return 0;
		}
	}
	if (*out_start < statement_start || *out_end > statement_end ||
	    *out_start >= *out_end) {
		*out_start = 0U;
		*out_end = 0U;
		return 0;
	}
	left_delimiter = *out_start;
	while (left_delimiter > statement_start &&
	       isspace((unsigned char)handle->sql[left_delimiter - 1U])) {
		left_delimiter--;
	}
	right_delimiter = *out_end;
	while (right_delimiter < statement_end &&
	       isspace((unsigned char)handle->sql[right_delimiter])) {
		right_delimiter++;
	}
	if (left_delimiter == statement_start ||
	    (handle->sql[left_delimiter - 1U] != '(' &&
	     handle->sql[left_delimiter - 1U] != ',') ||
	    right_delimiter >= statement_end ||
	    (handle->sql[right_delimiter] != ',' &&
	     handle->sql[right_delimiter] != ')')) {
		*out_start = 0U;
		*out_end = 0U;
		return 0;
	}
	for (pos = *out_start; pos < *out_end;) {
		skipped = sqlparser_public_skip_quoted_or_comment(
			handle->dialect,
			handle->sql,
			pos);
		if (skipped == pos) {
			pos++;
			continue;
		}
		if ((!allow_comments && sqlparser_public_comment_at(
			     handle->dialect, handle->sql, pos)) ||
		    skipped > *out_end) {
			*out_start = 0U;
			*out_end = 0U;
			return 0;
		}
		pos = skipped;
	}
	return 1;
}

static size_t sqlparser_view_merge_skip_trivia(
	const sqlparser_handle_t *handle,
	const char *sql,
	size_t pos,
	size_t end)
{
	pos = sqlparser_public_skip_trivia(
		handle->dialect,
		sql,
		pos);
	return pos < end ? pos : end;
}

static int sqlparser_view_merge_when_header(
	const sqlparser_handle_t *handle,
	const char *sql,
	size_t when_pos,
	size_t end,
	size_t *out_after_header)
{
	size_t pos;

	pos = sqlparser_view_merge_skip_trivia(
		handle,
		sql,
		when_pos + strlen("when"),
		end);
	if (sqlparser_view_public_word_at(sql, pos, end, "matched")) {
		pos += strlen("matched");
	} else if (sqlparser_view_public_word_at(sql, pos, end, "not")) {
		pos = sqlparser_view_merge_skip_trivia(
			handle,
			sql,
			pos + strlen("not"),
			end);
		if (!sqlparser_view_public_word_at(
			    sql,
			    pos,
			    end,
			    "matched")) {
			return 0;
		}
		pos += strlen("matched");
	} else {
		return 0;
	}
	pos = sqlparser_view_merge_skip_trivia(
		handle,
		sql,
		pos,
		end);
	if (sqlparser_view_public_word_at(sql, pos, end, "by")) {
		pos = sqlparser_view_merge_skip_trivia(
			handle,
			sql,
			pos + strlen("by"),
			end);
		if (sqlparser_view_public_word_at(
			    sql,
			    pos,
			    end,
			    "target")) {
			pos += strlen("target");
		} else if (sqlparser_view_public_word_at(
				   sql,
				   pos,
				   end,
				   "source")) {
			pos += strlen("source");
		} else {
			return 0;
		}
		pos = sqlparser_view_merge_skip_trivia(
			handle,
			sql,
			pos,
			end);
	}
	*out_after_header = pos;
	return 1;
}

static int sqlparser_view_merge_action_condition_span(
	const sqlparser_handle_t *handle,
	const char *sql,
	size_t after_header,
	size_t end,
	size_t base_paren_depth,
	sqlparser_selector_kind_t role,
	size_t *out_start,
	size_t *out_end)
{
	size_t case_depth;
	size_t condition_start;
	size_t paren_depth;
	size_t pos;
	size_t skipped;
	int then_seen;
	int where_seen;

	if (handle == NULL || sql == NULL || out_start == NULL ||
	    out_end == NULL ||
	    (role != SQLPARSER_SELECTOR_KIND_MERGE_BRANCH_CONDITION &&
	     role != SQLPARSER_SELECTOR_KIND_MERGE_DELETE_CONDITION)) {
		return 0;
	}
	case_depth = 0U;
	condition_start = 0U;
	paren_depth = base_paren_depth;
	then_seen = 0;
	where_seen = 0;
	for (pos = after_header; pos < end; pos++) {
		size_t ignored_header;

		skipped = sqlparser_public_skip_quoted_or_comment(
			handle->dialect,
			sql,
			pos);
		if (skipped != pos) {
			pos = skipped - 1U;
			continue;
		}
		if (sql[pos] == '(') {
			paren_depth++;
			continue;
		}
		if (sql[pos] == ')' && paren_depth > 0U) {
			paren_depth--;
			continue;
		}
		if (sqlparser_view_public_word_at(
			    sql,
			    pos,
			    end,
			    "case")) {
			case_depth++;
			pos += strlen("case") - 1U;
			continue;
		}
		if (case_depth > 0U &&
		    sqlparser_view_public_word_at(
			    sql,
			    pos,
			    end,
			    "end")) {
			case_depth--;
			pos += strlen("end") - 1U;
			continue;
		}
		if (paren_depth != base_paren_depth || case_depth != 0U) {
			continue;
		}
		if (!then_seen) {
			if (sqlparser_view_public_word_at(
				    sql,
				    pos,
				    end,
				    "then")) {
				then_seen = 1;
				pos += strlen("then") - 1U;
			}
			continue;
		}
		if (sqlparser_view_public_word_at(
			    sql,
			    pos,
			    end,
			    "when") &&
		    sqlparser_view_merge_when_header(
			    handle,
			    sql,
			    pos,
			    end,
			    &ignored_header)) {
			end = pos;
			break;
		}
		if (role ==
			    SQLPARSER_SELECTOR_KIND_MERGE_DELETE_CONDITION &&
		    !where_seen &&
		    sqlparser_view_public_word_at(
			    sql,
			    pos,
			    end,
			    "delete")) {
			size_t after_delete;

			after_delete = sqlparser_view_merge_skip_trivia(
				handle,
				sql,
				pos + strlen("delete"),
				end);
			if (sqlparser_view_public_word_at(
				    sql,
				    after_delete,
				    end,
				    "where")) {
				condition_start = sqlparser_public_skip_space(
					sql,
					after_delete + strlen("where"));
				where_seen = 1;
				pos = after_delete + strlen("where") - 1U;
			}
			continue;
		}
		if (role ==
			    SQLPARSER_SELECTOR_KIND_MERGE_BRANCH_CONDITION &&
		    !where_seen &&
		    sqlparser_view_public_word_at(
			    sql,
			    pos,
			    end,
			    "where")) {
			condition_start = sqlparser_public_skip_space(
				sql,
				pos + strlen("where"));
			where_seen = 1;
			pos += strlen("where") - 1U;
			continue;
		}
		if (role ==
			    SQLPARSER_SELECTOR_KIND_MERGE_BRANCH_CONDITION &&
		    where_seen &&
		    sqlparser_view_public_word_at(
			    sql,
			    pos,
			    end,
			    "delete")) {
			size_t after_delete;

			after_delete = sqlparser_view_merge_skip_trivia(
				handle,
				sql,
				pos + strlen("delete"),
				end);
			if (sqlparser_view_public_word_at(
				    sql,
				    after_delete,
				    end,
				    "where")) {
				end = pos;
				break;
			}
		}
		if (where_seen &&
		    sqlparser_view_public_word_at(
			    sql,
			    pos,
			    end,
			    "log")) {
			size_t after_log;

			after_log = sqlparser_view_merge_skip_trivia(
				handle,
				sql,
				pos + strlen("log"),
				end);
			if (sqlparser_view_public_word_at(
				    sql,
				    after_log,
				    end,
				    "errors")) {
				end = pos;
				break;
			}
		}
	}
	while (end > condition_start &&
	       isspace((unsigned char)sql[end - 1U])) {
		end--;
	}
	if (!then_seen || !where_seen || condition_start >= end) {
		return 0;
	}
	*out_start = condition_start;
	*out_end = end;
	return 1;
}

static int sqlparser_view_cte_query_source_span(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	const PgQuery__CommonTableExpr *cte,
	size_t *out_start,
	size_t *out_end,
	sqlparser_error_t *out_error)
{
	const char *sql;
	sqlparser_identifier_origin_t origin;
	const sqlparser_identifier_origin_map_t *origins;
	sqlparser_status_t status;
	size_t depth;
	size_t end;
	size_t pos;
	size_t skipped;
	size_t start;
	int saw_as;

	if (handle == NULL || cte == NULL || cte->location < 0 ||
	    out_start == NULL || out_end == NULL ||
	    !sqlparser_view_public_statement_span(
		    handle, statement_index, &start, &end)) {
		return 0;
	}
	status = sqlparser_identifier_origins_for_handle(
		handle, &origins, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return -1;
	}
	if (sqlparser_identifier_origin_map_lookup(
		    origins,
		    (size_t)cte->location,
		    1U,
		    &origin) != SQLPARSER_IDENTIFIER_ORIGIN_SOURCE ||
	    origin.source_offset < start || origin.source_offset >= end) {
		return 0;
	}
	sql = sqlparser_effective_sql(handle);
	depth = 0U;
	saw_as = 0;
	for (pos = origin.source_offset; pos < end; pos++) {
		skipped = sqlparser_public_skip_quoted_or_comment(
			handle->dialect, sql, pos);
		if (skipped != pos) {
			pos = skipped - 1U;
			continue;
		}
		if (!saw_as && depth == 0U &&
		    sqlparser_view_public_word_at(sql, pos, end, "as")) {
			saw_as = 1;
			pos += strlen("as") - 1U;
			continue;
		}
		if (sql[pos] == '(') {
			if (saw_as && depth == 0U) {
				start = pos + 1U;
				depth = 1U;
				for (pos = start; pos < end; pos++) {
					skipped =
						sqlparser_public_skip_quoted_or_comment(
							handle->dialect, sql, pos);
					if (skipped != pos) {
						pos = skipped - 1U;
						continue;
					}
					if (sql[pos] == '(') {
						depth++;
					} else if (sql[pos] == ')' && --depth == 0U) {
						*out_start = start;
						*out_end = pos;
						return 1;
					}
				}
				return 0;
			}
			depth++;
		} else if (sql[pos] == ')' && depth > 0U) {
			depth--;
		}
	}
	return 0;
}

static sqlparser_status_t sqlparser_merge_condition_node(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	size_t dml_index,
	size_t when_index,
	sqlparser_selector_kind_t role,
	PgQuery__Node **out_condition,
	sqlparser_error_t *out_error)
{
	PgQuery__MergeStmt *merge_stmt;
	PgQuery__MergeWhenClause *when_clause;
	PgQuery__Node *when_node;
	PgQuery__Node *condition;
	sqlparser_status_t status;

	if (out_condition != NULL) {
		*out_condition = NULL;
	}
	if (handle == NULL ||
	    (role != SQLPARSER_SELECTOR_KIND_MERGE_BRANCH_CONDITION &&
	     role != SQLPARSER_SELECTOR_KIND_MERGE_DELETE_CONDITION)) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"MERGE condition selector is invalid");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	merge_stmt = NULL;
	status = sqlparser_get_merge_stmt_by_dml_index(
		(sqlparser_handle_t *)handle,
		statement_index,
		dml_index,
		&merge_stmt,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	if (when_index >= merge_stmt->n_merge_when_clauses ||
	    merge_stmt->merge_when_clauses == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"merge WHEN index is out of range");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	when_node = merge_stmt->merge_when_clauses[when_index];
	if (when_node == NULL ||
	    when_node->node_case != PG_QUERY__NODE__NODE_MERGE_WHEN_CLAUSE ||
	    when_node->merge_when_clause == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INTERNAL_ERROR,
			"MERGE WHEN node is invalid");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}
	when_clause = when_node->merge_when_clause;
	if (role == SQLPARSER_SELECTOR_KIND_MERGE_DELETE_CONDITION &&
	    when_clause->command_type != PG_QUERY__CMD_TYPE__CMD_UPDATE) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"merge DELETE condition does not target an UPDATE action");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	condition =
		role == SQLPARSER_SELECTOR_KIND_MERGE_DELETE_CONDITION ?
			when_clause->delete_condition :
			when_clause->condition;
	if (condition == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			role ==
					SQLPARSER_SELECTOR_KIND_MERGE_DELETE_CONDITION ?
				"merge UPDATE action has no DELETE condition" :
				"merge WHEN action has no condition");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	if (out_condition != NULL) {
		*out_condition = condition;
	}
	return SQLPARSER_STATUS_OK;
}

int sqlparser_merge_condition_source_span(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	size_t dml_index,
	size_t when_index,
	sqlparser_selector_kind_t role,
	const char **out_source_sql,
	size_t *out_start,
	size_t *out_end,
	sqlparser_error_t *out_error)
{
	const char *sql;
	PgQuery__Node *statement;
	sqlparser_dialect_dml_result_dml_t dml;
	sqlparser_status_t ast_status;
	int span_status;
	size_t base_paren_depth;
	size_t case_depth;
	size_t current_when;
	size_t end;
	size_t paren_depth;
	size_t pos;
	size_t skipped;
	size_t start;
	int merge_seen;

	sqlparser_error_clear(out_error);
	if (out_source_sql == NULL || out_start == NULL || out_end == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"MERGE condition source span outputs must not be NULL");
		return -1;
	}
	*out_source_sql = NULL;
	*out_start = 0U;
	*out_end = 0U;
	ast_status = sqlparser_merge_condition_node(
		handle,
		statement_index,
		dml_index,
		when_index,
		role,
		NULL,
		out_error);
	if (ast_status != SQLPARSER_STATUS_OK) {
		return -1;
	}
	statement = NULL;
	if (dml_index == 0U) {
		ast_status = sqlparser_get_statement_node(
			(sqlparser_handle_t *)handle,
			statement_index,
			&statement,
			out_error);
		if (ast_status != SQLPARSER_STATUS_OK) {
			return -1;
		}
		if (statement != NULL &&
		    statement->node_case == PG_QUERY__NODE__NODE_MERGE_STMT &&
		    statement->merge_stmt != NULL) {
			if (!sqlparser_view_public_statement_span(
				    handle,
				    statement_index,
				    &start,
				    &end)) {
				return 0;
			}
			sql = sqlparser_effective_sql(handle);
			goto scan;
		}
	}
	memset(&dml, 0, sizeof(dml));
	if (!sqlparser_dialect_dml_result_dml_at(
		    handle,
		    statement_index,
		    dml_index,
		    &dml) ||
	    dml.kind != SQLPARSER_GRAPH_DML_MERGE) {
		return 0;
	}
	if (dml.message != NULL && dml.cte != NULL) {
		span_status = sqlparser_view_cte_query_source_span(
			    handle,
			    statement_index,
			    dml.cte,
			    &start,
			    &end,
			    out_error);
		if (span_status <= 0) {
			return span_status;
		}
		sql = sqlparser_effective_sql(handle);
	} else {
		if (dml.source_sql == NULL) {
			return 0;
		}
		sql = dml.source_sql;
		start = 0U;
		end = strlen(sql);
	}

scan:
	base_paren_depth = 0U;
	case_depth = 0U;
	current_when = 0U;
	paren_depth = 0U;
	merge_seen = 0;
	for (pos = start; pos < end; pos++) {
		size_t after_header;

		skipped = sqlparser_public_skip_quoted_or_comment(
			handle->dialect,
			sql,
			pos);
		if (skipped != pos) {
			pos = skipped - 1U;
			continue;
		}
		if (sql[pos] == '(') {
			paren_depth++;
			continue;
		}
		if (sql[pos] == ')' && paren_depth > 0U) {
			paren_depth--;
			continue;
		}
		if (!merge_seen) {
			if (paren_depth == 0U &&
			    sqlparser_view_public_word_at(
				    sql,
				    pos,
				    end,
				    "merge")) {
				merge_seen = 1;
				base_paren_depth = paren_depth;
				pos += strlen("merge") - 1U;
			}
			continue;
		}
		if (sqlparser_view_public_word_at(
			    sql,
			    pos,
			    end,
			    "case")) {
			case_depth++;
			pos += strlen("case") - 1U;
			continue;
		}
		if (case_depth > 0U &&
		    sqlparser_view_public_word_at(
			    sql,
			    pos,
			    end,
			    "end")) {
			case_depth--;
			pos += strlen("end") - 1U;
			continue;
		}
		if (paren_depth != base_paren_depth ||
		    case_depth != 0U ||
		    !sqlparser_view_public_word_at(
			    sql,
			    pos,
			    end,
			    "when") ||
		    !sqlparser_view_merge_when_header(
			    handle,
			    sql,
			    pos,
			    end,
			    &after_header)) {
			continue;
		}
		if (current_when++ != when_index) {
			pos += strlen("when") - 1U;
			continue;
		}
		if (role ==
			    SQLPARSER_SELECTOR_KIND_MERGE_DELETE_CONDITION ||
		    !sqlparser_view_public_word_at(
			    sql,
			    after_header,
			    end,
			    "and")) {
			span_status =
				sqlparser_view_merge_action_condition_span(
				handle,
				sql,
				after_header,
				end,
				base_paren_depth,
				role,
				out_start,
				out_end);
			if (span_status > 0) {
				*out_source_sql = sql;
			}
			return span_status;
		}
		start = sqlparser_public_skip_space(
			sql,
			after_header + strlen("and"));
		case_depth = 0U;
		for (pos = start; pos < end; pos++) {
			skipped =
				sqlparser_public_skip_quoted_or_comment(
					handle->dialect,
					sql,
					pos);
			if (skipped != pos) {
				pos = skipped - 1U;
				continue;
			}
			if (sql[pos] == '(') {
				paren_depth++;
				continue;
			}
			if (sql[pos] == ')' && paren_depth > 0U) {
				paren_depth--;
				continue;
			}
			if (sqlparser_view_public_word_at(
				    sql,
				    pos,
				    end,
				    "case")) {
				case_depth++;
				pos += strlen("case") - 1U;
				continue;
			}
			if (case_depth > 0U &&
			    sqlparser_view_public_word_at(
				    sql,
				    pos,
				    end,
				    "end")) {
				case_depth--;
				pos += strlen("end") - 1U;
				continue;
			}
			if (paren_depth == base_paren_depth &&
			    case_depth == 0U &&
			    sqlparser_view_public_word_at(
				    sql,
				    pos,
				    end,
				    "then")) {
				size_t condition_end;

				condition_end = pos;
				while (condition_end > start &&
				       isspace((unsigned char)
						       sql[condition_end - 1U])) {
					condition_end--;
				}
				if (start >= condition_end) {
					return 0;
				}
				*out_source_sql = sql;
				*out_start = start;
				*out_end = condition_end;
				return 1;
			}
		}
		return 0;
	}
	return 0;
}

static const PgQuery__Node *
sqlparser_view_merge_action_where_expression(const PgQuery__Node *node)
{
	const PgQuery__BoolExpr *expression;

	node = sqlparser_unwrap_grouping_node_const(node);
	if (node == NULL ||
	    node->node_case != PG_QUERY__NODE__NODE_BOOL_EXPR ||
	    node->bool_expr == NULL) {
		return NULL;
	}
	expression = node->bool_expr;
	if (expression->boolop != PG_QUERY__BOOL_EXPR_TYPE__AND_EXPR ||
	    expression->location !=
		    POSTGRES_DEPARSE_MERGE_ACTION_WHERE_LOCATION ||
	    expression->n_args != 1U ||
	    expression->args == NULL) {
		return NULL;
	}
	return expression->args[0];
}

static sqlparser_status_t sqlparser_view_render_merge_condition_sql(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	const PgQuery__Node *condition,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	PgQuery__BoolExpr visible_bool;
	PgQuery__Node visible_node;
	const PgQuery__Node *action_where;
	const PgQuery__Node *visible_condition;
	char *core_sql;
	size_t visible_arg_count;
	sqlparser_status_t status;

	visible_condition = condition;
	action_where =
		sqlparser_view_merge_action_where_expression(condition);
	if (action_where != NULL) {
		visible_condition = action_where;
	} else if (condition != NULL &&
		   condition->node_case ==
			   PG_QUERY__NODE__NODE_BOOL_EXPR &&
		   condition->bool_expr != NULL &&
		   condition->bool_expr->boolop ==
			   PG_QUERY__BOOL_EXPR_TYPE__AND_EXPR &&
		   condition->bool_expr->n_args > 1U &&
		   condition->bool_expr->args != NULL &&
		   sqlparser_view_merge_action_where_expression(
			   condition->bool_expr->args[
				   condition->bool_expr->n_args - 1U]) !=
			   NULL) {
		visible_arg_count =
			condition->bool_expr->n_args - 1U;
		if (visible_arg_count == 1U) {
			visible_condition =
				condition->bool_expr->args[0];
		} else {
			visible_node = *condition;
			visible_bool = *condition->bool_expr;
			visible_bool.n_args = visible_arg_count;
			visible_node.bool_expr = &visible_bool;
			visible_condition = &visible_node;
		}
	}
	if (visible_condition == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INTERNAL_ERROR,
			"MERGE WHEN condition is invalid");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}
	core_sql = NULL;
	status = sqlparser_render_where_node_sql(
		handle,
		visible_condition,
		&core_sql,
		out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_postprocess_handle_sql_fragment(
			handle,
			statement_index,
			core_sql,
			SQLPARSER_FRAGMENT_CONTEXT_EXPRESSION,
			(ProtobufCMessage *const *)&visible_condition,
			1U,
			"MERGE WHEN condition",
			out_sql,
			out_error);
	}
	free(core_sql);
	return status;
}

static sqlparser_status_t sqlparser_merge_condition_sql(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	size_t dml_index,
	size_t when_index,
	sqlparser_selector_kind_t role,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	const char *source_sql;
	PgQuery__Node *condition;
	sqlparser_status_t ast_status;
	size_t end;
	size_t start;
	int status;

	sqlparser_error_clear(out_error);
	if (handle == NULL || out_sql == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"handle and out_sql must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_sql = NULL;
	if (handle->generation == 0UL) {
		source_sql = NULL;
		start = 0U;
		end = 0U;
		status = sqlparser_merge_condition_source_span(
			handle,
			statement_index,
			dml_index,
			when_index,
			role,
			&source_sql,
			&start,
			&end,
			out_error);
		if (status > 0 && source_sql != NULL && start < end) {
			*out_sql = sqlparser_strndup(
				source_sql + start,
				end - start);
			if (*out_sql == NULL) {
				sqlparser_error_set_message(
					out_error,
					SQLPARSER_STATUS_NO_MEMORY,
					"out of memory");
				return SQLPARSER_STATUS_NO_MEMORY;
			}
			return SQLPARSER_STATUS_OK;
		}
		if (status < 0) {
			return out_error != NULL ?
				out_error->code :
				SQLPARSER_STATUS_INTERNAL_ERROR;
		}
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			role ==
					SQLPARSER_SELECTOR_KIND_MERGE_DELETE_CONDITION ?
				"MERGE DELETE condition is missing or cannot be located" :
				"MERGE WHEN condition is missing or cannot be located");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	condition = NULL;
	ast_status = sqlparser_merge_condition_node(
		handle,
		statement_index,
		dml_index,
		when_index,
		role,
		&condition,
		out_error);
	if (ast_status != SQLPARSER_STATUS_OK) {
		return ast_status;
	}
	return sqlparser_view_render_merge_condition_sql(
		handle,
		statement_index,
		condition,
		out_sql,
		out_error);
}

sqlparser_status_t sqlparser_merge_branch_condition_sql(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	size_t dml_index,
	size_t when_index,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	return sqlparser_merge_condition_sql(
		handle,
		statement_index,
		dml_index,
		when_index,
		SQLPARSER_SELECTOR_KIND_MERGE_BRANCH_CONDITION,
		out_sql,
		out_error);
}

sqlparser_status_t sqlparser_merge_delete_condition_sql(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	size_t dml_index,
	size_t when_index,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	return sqlparser_merge_condition_sql(
		handle,
		statement_index,
		dml_index,
		when_index,
		SQLPARSER_SELECTOR_KIND_MERGE_DELETE_CONDITION,
		out_sql,
		out_error);
}

static int sqlparser_view_node_source_location(
	const PgQuery__Node *node,
	size_t *out_location)
{
	const ProtobufCMessageDescriptor *descriptor;
	const ProtobufCMessage *message;
	const uint8_t *base;
	unsigned index;

	if (node == NULL || out_location == NULL) {
		return 0;
	}
	message = (const ProtobufCMessage *)node;
	descriptor = message->descriptor;
	if (descriptor == NULL) {
		return 0;
	}
	base = (const uint8_t *)message;
	for (index = 0U; index < descriptor->n_fields; index++) {
		const ProtobufCFieldDescriptor *field;
		ProtobufCMessage *child;
		int32_t *location;

		field = &descriptor->fields[index];
		if (field->type != PROTOBUF_C_TYPE_MESSAGE ||
		    field->label == PROTOBUF_C_LABEL_REPEATED ||
		    ((field->flags & PROTOBUF_C_FIELD_FLAG_ONEOF) != 0U &&
		     *(const int *)(base + field->quantifier_offset) !=
			     (int)field->id)) {
			continue;
		}
		child = *(ProtobufCMessage *const *)(base + field->offset);
		location = sqlparser_proto_location_slot(child);
		if (location != NULL && *location >= 0) {
			*out_location = (size_t)*location;
			return 1;
		}
		if (child != NULL &&
		    child->descriptor == &pg_query__type_cast__descriptor &&
		    sqlparser_view_node_source_location(
			    ((const PgQuery__TypeCast *)child)->arg,
			    out_location)) {
			return 1;
		}
	}
	return 0;
}

static int sqlparser_view_source_delimiter(
	const sqlparser_identifier_origin_map_t *origins,
	size_t parser_offset,
	size_t *out_start,
	size_t *out_end)
{
	sqlparser_identifier_origin_t origin;

	if (out_start == NULL || out_end == NULL ||
	    sqlparser_identifier_origin_map_lookup(
		    origins,
		    parser_offset,
		    1U,
		    &origin) != SQLPARSER_IDENTIFIER_ORIGIN_SOURCE ||
	    origin.source_length == 0U ||
	    origin.source_offset > (size_t)-1 - origin.source_length) {
		return 0;
	}
	*out_start = origin.source_offset;
	*out_end = origin.source_offset + origin.source_length;
	return 1;
}

static int sqlparser_view_expression_source_span_between(
	const sqlparser_handle_t *handle,
	const sqlparser_identifier_origin_map_t *origins,
	const sqlparser_surface_source_edits_t *surface_edits,
	size_t parser_left_delimiter,
	size_t parser_right_delimiter,
	size_t *out_start,
	size_t *out_end)
{
	size_t edit_index;
	size_t left_end;
	size_t left_start;
	size_t right_end;
	size_t right_start;
	size_t source_end;
	size_t source_start;

	if (!sqlparser_view_source_delimiter(
		    origins,
		    parser_left_delimiter,
		    &left_start,
		    &left_end) ||
	    !sqlparser_view_source_delimiter(
		    origins,
		    parser_right_delimiter,
		    &right_start,
		    &right_end) ||
	    left_end > right_start ||
	    right_end > handle->sql_len) {
		return 0;
	}
	source_start = sqlparser_public_skip_space(
		handle->sql,
		left_end);
	source_end = right_start;
	while (source_end > source_start &&
	       isspace((unsigned char)handle->sql[source_end - 1U])) {
		source_end--;
	}
	if (source_start >= source_end) {
		return 0;
	}
	if (surface_edits != NULL) {
		for (edit_index = 0U;
		     edit_index < surface_edits->count;
		     edit_index++) {
			const sqlparser_surface_source_edit_t *edit;

			edit = &surface_edits->items[edit_index];
			if (edit->replacement == NULL ||
			    edit->source_start > edit->source_end ||
			    edit->source_end > handle->sql_len) {
				return 0;
			}
			if (edit->source_start == edit->source_end) {
				if (edit->source_start >= source_start &&
				    edit->source_start <= source_end) {
					return 0;
				}
			} else if (edit->source_start < source_end &&
				   source_start < edit->source_end) {
				return 0;
			}
		}
	}
	*out_start = source_start;
	*out_end = source_end;
	return 1;
}

static int sqlparser_view_expression_source_span_in_values(
	const sqlparser_handle_t *handle,
	const sqlparser_identifier_origin_map_t *origins,
	const sqlparser_surface_source_edits_t *surface_edits,
	size_t scan_start,
	size_t location,
	size_t *out_resume,
	size_t *out_start,
	size_t *out_end)
{
	const char *parser_sql;
	size_t brace_depth;
	size_t bracket_depth;
	size_t cell_left_delimiter;
	size_t paren_depth;
	size_t pos;
	size_t skipped;

	parser_sql = handle->parser_sql;
	pos = sqlparser_public_skip_trivia(
		SQLPARSER_DIALECT_POSTGRESQL,
		parser_sql,
		scan_start);
	if (parser_sql[pos] != '(' && parser_sql[pos] != ',') {
		return 0;
	}
	for (;;) {
		cell_left_delimiter = pos;
		paren_depth = 0U;
		bracket_depth = 0U;
		brace_depth = 0U;
		for (pos++; parser_sql[pos] != '\0'; pos++) {
			skipped = sqlparser_public_skip_quoted_or_comment(
				SQLPARSER_DIALECT_POSTGRESQL,
				parser_sql,
				pos);
			if (skipped != pos) {
				pos = skipped - 1U;
				continue;
			}
			if (parser_sql[pos] == '(') {
				paren_depth++;
				continue;
			}
			if (parser_sql[pos] == '[') {
				bracket_depth++;
				continue;
			}
			if (parser_sql[pos] == '{') {
				brace_depth++;
				continue;
			}
			if (parser_sql[pos] == ')' && paren_depth > 0U) {
				paren_depth--;
				continue;
			}
			if (parser_sql[pos] == ']' && bracket_depth > 0U) {
				bracket_depth--;
				continue;
			}
			if (parser_sql[pos] == '}' && brace_depth > 0U) {
				brace_depth--;
				continue;
			}
			if ((parser_sql[pos] != ',' && parser_sql[pos] != ')') ||
			    paren_depth != 0U ||
			    bracket_depth != 0U ||
			    brace_depth != 0U) {
				continue;
			}
			if (location > cell_left_delimiter && location < pos) {
				if (out_resume != NULL) {
					*out_resume = cell_left_delimiter;
				}
				return sqlparser_view_expression_source_span_between(
					handle,
					origins,
					surface_edits,
					cell_left_delimiter,
					pos,
					out_start,
					out_end);
			}
			if (parser_sql[pos] == ',') {
				cell_left_delimiter = pos;
				continue;
			}
			pos = sqlparser_public_skip_trivia(
				SQLPARSER_DIALECT_POSTGRESQL,
				parser_sql,
				pos + 1U);
			if (parser_sql[pos] != ',') {
				return 0;
			}
			pos = sqlparser_public_skip_trivia(
				SQLPARSER_DIALECT_POSTGRESQL,
				parser_sql,
				pos + 1U);
			if (parser_sql[pos] != '(') {
				return 0;
			}
			break;
		}
		if (parser_sql[pos] == '\0') {
			return 0;
		}
	}
}

static int sqlparser_view_expression_source_span(
	const sqlparser_handle_t *handle,
	const sqlparser_identifier_origin_map_t **origins,
	sqlparser_view_expression_source_cache_t *cache,
	PgQuery__Node *value_node,
	const sqlparser_surface_source_edits_t *surface_edits,
	size_t *out_start,
	size_t *out_end,
	sqlparser_error_t *out_error)
{
	const char *parser_sql;
	size_t location;
	size_t pos;
	size_t resume;
	size_t search_position;
	size_t skipped;
	sqlparser_status_t status;

	if (out_start == NULL || out_end == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"expression source span must not be NULL");
		return -1;
	}
	*out_start = 0U;
	*out_end = 0U;
	if (handle == NULL || origins == NULL || value_node == NULL ||
	    !sqlparser_view_node_source_location(value_node, &location) ||
	    location >= handle->parser_sql_len) {
		return 0;
	}
	if (*origins == NULL) {
		status = sqlparser_identifier_origins_for_handle(
			handle,
			origins,
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			return -1;
		}
	}
	parser_sql = handle->parser_sql;
	search_position = 0U;
	if (cache != NULL &&
	    cache->valid &&
	    location >= cache->last_location) {
		int source_status;

		resume = cache->resume;
		source_status = sqlparser_view_expression_source_span_in_values(
			handle,
			*origins,
			surface_edits,
			resume,
			location,
			&resume,
			out_start,
			out_end);
		if (source_status != 0) {
			cache->resume = resume;
			cache->last_location = location;
			return source_status;
		}
		search_position = cache->search_position;
		cache->valid = 0;
	} else if (cache != NULL) {
		cache->valid = 0;
	}
	for (pos = search_position;
	     parser_sql[pos] != '\0';
	     pos++) {
		int source_status;

		skipped = sqlparser_public_skip_quoted_or_comment(
			SQLPARSER_DIALECT_POSTGRESQL,
			parser_sql,
			pos);
		if (skipped != pos) {
			pos = skipped - 1U;
			continue;
		}
		if (!sqlparser_view_public_word_at(
			    parser_sql,
			    pos,
			    handle->parser_sql_len,
			    "values")) {
			continue;
		}
		source_status = sqlparser_view_expression_source_span_in_values(
			handle,
			*origins,
			surface_edits,
			pos + strlen("values"),
			location,
			&resume,
			out_start,
			out_end);
		if (source_status != 0) {
			if (cache != NULL) {
				cache->resume = resume;
				cache->search_position =
					pos + strlen("values");
				cache->last_location = location;
				cache->valid = 1;
			}
			return source_status;
		}
	}
	return 0;
}

static int sqlparser_view_expression_source_sql(
	const sqlparser_handle_t *handle,
	const sqlparser_identifier_origin_map_t **origins,
	sqlparser_view_expression_source_cache_t *cache,
	PgQuery__Node *value_node,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	const sqlparser_surface_source_edits_t *surface_edits;
	size_t source_end;
	size_t source_start;
	int source_status;

	if (out_sql == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"expression source output must not be NULL");
		return -1;
	}
	*out_sql = NULL;
	if (handle == NULL ||
	    (handle->generation != 0UL &&
	     !handle->surface_source_complete)) {
		return 0;
	}
	surface_edits = handle->generation != 0UL ?
		&handle->surface_source_edits : NULL;
	source_status = sqlparser_view_expression_source_span(
		handle,
		origins,
		cache,
		value_node,
		surface_edits,
		&source_start,
		&source_end,
		out_error);
	if (source_status <= 0) {
		return source_status;
	}
	*out_sql = sqlparser_strndup(
		handle->sql + source_start,
		source_end - source_start);
	if (*out_sql == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_NO_MEMORY,
			"out of memory");
		return -1;
	}
	return 1;
}

static int sqlparser_view_public_name_matches(
	const char *sql,
	size_t start,
	size_t end,
	const char *name)
{
	char close;
	char open;
	size_t name_index;
	size_t pos;

	if (sql == NULL || name == NULL || start >= end) {
		return 0;
	}
	open = sql[start];
	close = open == '[' ? ']' : open;
	if (open == '"' || open == '`' || open == '[') {
		name_index = 0U;
		pos = start + 1U;
		while (pos < end) {
			if (sql[pos] == close) {
				if (pos + 1U < end && sql[pos + 1U] == close) {
					if (name[name_index] == '\0' ||
					    name[name_index] != close) {
						return 0;
					}
					name_index++;
					pos += 2U;
					continue;
				}
				return name[name_index] == '\0';
			}
			if (name[name_index] == '\0' ||
			    sql[pos] != name[name_index]) {
				return 0;
			}
			name_index++;
			pos++;
		}
		return 0;
	}
	pos = start;
	name_index = 0U;
	while (pos < end &&
	       sqlparser_public_char_is_ident(
		       (unsigned char)sql[pos])) {
		if (name[name_index] == '\0' ||
		    tolower((unsigned char)sql[pos]) !=
			    tolower((unsigned char)name[name_index])) {
			return 0;
		}
		pos++;
		name_index++;
	}
	return pos > start && name[name_index] == '\0';
}

static int sqlparser_view_variable_set_source_name_matches(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	const char *name,
	int *out_parsed)
{
	const char *sql;
	size_t end;
	size_t pos;
	size_t start;
	size_t token_end;

	if (out_parsed != NULL) {
		*out_parsed = 0;
	}
	if (!sqlparser_view_public_statement_span(
		    handle,
		    statement_index,
		    &start,
		    &end)) {
		return 0;
	}
	sql = sqlparser_effective_sql(handle);
	pos = start;
	if (sqlparser_view_public_word_at(sql, pos, end, "set")) {
		pos += strlen("set");
	} else if (sqlparser_view_public_word_at(sql, pos, end, "reset")) {
		pos += strlen("reset");
	} else {
		if (out_parsed != NULL) {
			*out_parsed = 1;
		}
		return 0;
	}
	pos = sqlparser_public_skip_trivia(handle->dialect, sql, pos);
	if (sqlparser_view_public_word_at(sql, pos, end, "local")) {
		pos += strlen("local");
		pos = sqlparser_public_skip_trivia(handle->dialect, sql, pos);
	} else if (sqlparser_view_public_word_at(sql, pos, end, "session")) {
		pos += strlen("session");
		pos = sqlparser_public_skip_trivia(handle->dialect, sql, pos);
	}
	if (pos + 1U < end && sql[pos] == '@' && sql[pos + 1U] == '@') {
		pos += 2U;
		token_end = pos;
		while (token_end < end &&
		       sqlparser_public_char_is_ident(
			       (unsigned char)sql[token_end])) {
			token_end++;
		}
		if (token_end < end && sql[token_end] == '.') {
			pos = token_end + 1U;
		}
	}
	if (pos + 2U < end &&
	    (sql[pos] == 'u' || sql[pos] == 'U') &&
	    sql[pos + 1U] == '&' &&
	    sql[pos + 2U] == '"') {
		return 0;
	}
	token_end = pos;
	if (pos < end &&
	    (sql[pos] == '"' || sql[pos] == '`' || sql[pos] == '[')) {
		token_end = sqlparser_public_skip_quoted_or_comment(
			handle->dialect,
			sql,
			pos);
	} else {
		while (token_end < end &&
		       sqlparser_public_char_is_ident(
			       (unsigned char)sql[token_end])) {
			token_end++;
		}
	}
	if (token_end <= pos) {
		return 0;
	}
	if (out_parsed != NULL) {
		*out_parsed = 1;
	}
	return sqlparser_view_public_name_matches(
		sql,
		pos,
		token_end,
		name);
}

static int sqlparser_view_variable_set_is_internal_rewrite(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	const PgQuery__VariableSetStmt *stmt)
{
	const PgQuery__Node *arg;
	char *public_raw_sql;
	const char *raw_sql;
	const char *sql;
	sqlparser_error_t ignored_error;
	size_t end;
	size_t start;
	int source_name_parsed;

	if (handle == NULL || stmt == NULL || stmt->name == NULL ||
	    !sqlparser_variable_set_name_is_internal(stmt)) {
		return 0;
	}
	sql = sqlparser_effective_sql(handle);
	if (sqlparser_view_public_statement_span(
		    handle,
		    statement_index,
		    &start,
		    &end) &&
	    stmt->n_args == 1U &&
	    stmt->args != NULL) {
		if (handle->dialect == SQLPARSER_DIALECT_MYSQL &&
		    strcmp(
			    stmt->name,
			    SQLPARSER_INTERNAL_MYSQL_SESSION_STATEMENT) == 0 &&
		    sqlparser_mysql_public_sql_is_session_statement(
			    sql + start,
			    end - start)) {
			return 1;
		}
		arg = stmt->args[0];
		if (arg != NULL &&
		    arg->node_case == PG_QUERY__NODE__NODE_A_CONST &&
		    arg->a_const != NULL &&
		    arg->a_const->val_case == PG_QUERY__A__CONST__VAL_SVAL &&
		    arg->a_const->sval != NULL &&
		    arg->a_const->sval->sval != NULL) {
			raw_sql = arg->a_const->sval->sval;
			if (strlen(raw_sql) == end - start &&
			    memcmp(raw_sql, sql + start, end - start) == 0) {
				return 1;
			}
			public_raw_sql = NULL;
			memset(&ignored_error, 0, sizeof(ignored_error));
			if (sqlparser_postprocess_handle_sql_fragment(
				    handle,
				    statement_index,
				    raw_sql,
				    SQLPARSER_FRAGMENT_CONTEXT_OPAQUE,
				    NULL,
				    0U,
				    "internal rewrite",
				    &public_raw_sql,
				    &ignored_error) == SQLPARSER_STATUS_OK &&
			    public_raw_sql != NULL &&
			    strlen(public_raw_sql) == end - start &&
			    memcmp(public_raw_sql, sql + start, end - start) == 0) {
				free(public_raw_sql);
				return 1;
			}
			free(public_raw_sql);
		}
	}
	source_name_parsed = 0;
	if (sqlparser_view_variable_set_source_name_matches(
		    handle,
		    statement_index,
		    stmt->name,
		    &source_name_parsed)) {
		return 0;
	}
	return source_name_parsed;
}

static int sqlparser_view_parser_sql_has_bind(const char *sql)
{
	size_t index;

	for (index = 0U; sql != NULL && sql[index] != '\0';) {
		size_t skipped;

		skipped = sqlparser_public_skip_quoted_or_comment(
			SQLPARSER_DIALECT_POSTGRESQL,
			sql,
			index);
		if (skipped != index) {
			index = skipped;
			continue;
		}
		if (sql[index] == '$' &&
		    isdigit((unsigned char)sql[index + 1U]) &&
		    (index == 0U ||
		     !sqlparser_public_char_is_ident(
			     (unsigned char)sql[index - 1U]))) {
			return 1;
		}
		index++;
	}
	return 0;
}

static void sqlparser_view_bind_position_cache_release(
	sqlparser_view_bind_position_cache_t *cache)
{
	size_t index;

	if (cache == NULL) {
		return;
	}
	for (index = 0U; index < cache->count; index++) {
		free(cache->entries[index].public_sql);
	}
	free(cache->pointer_entries);
	free(cache->entries);
	memset(cache, 0, sizeof(*cache));
}

static sqlparser_status_t sqlparser_view_bind_position_render_public(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	const PgQuery__ParamRef *param_ref,
	char **out_public_sql,
	sqlparser_error_t *out_error)
{
	char core_token[32];

	if (out_public_sql == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"bind SQL output must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_public_sql = NULL;
	if (handle == NULL || param_ref == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"bind position entry is missing");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	if (param_ref->number <= 0) {
		*out_public_sql = sqlparser_strdup("?");
		if (*out_public_sql == NULL) {
			sqlparser_error_set_message(
				out_error,
				SQLPARSER_STATUS_NO_MEMORY,
				"out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		return SQLPARSER_STATUS_OK;
	}
	(void)snprintf(
		core_token,
		sizeof(core_token),
		"$%ld",
		(long)param_ref->number);
	return sqlparser_postprocess_handle_sql_fragment(
		handle,
		statement_index,
		core_token,
		SQLPARSER_FRAGMENT_CONTEXT_EXPRESSION,
		(ProtobufCMessage *const *)&param_ref,
		1U,
		"bind position",
		out_public_sql,
		out_error);
}

static sqlparser_status_t sqlparser_view_bind_position_append(
	sqlparser_handle_t *handle,
	size_t statement_index,
	PgQuery__ParamRef *param_ref,
	sqlparser_view_bind_position_cache_t *cache,
	sqlparser_error_t *out_error)
{
	sqlparser_view_bind_position_entry_t *entries;
	sqlparser_view_bind_position_entry_t *entry;
	size_t capacity;
	size_t public_length;
	sqlparser_status_t status;

	if (cache->count == cache->capacity) {
		capacity = cache->capacity > 0U ?
			cache->capacity * 2U :
			16U;
		if (capacity < cache->capacity ||
		    capacity > (size_t)-1 / sizeof(*entries)) {
			sqlparser_error_set_message(
				out_error,
				SQLPARSER_STATUS_RESOURCE_LIMIT,
				"bind position map is too large");
			return SQLPARSER_STATUS_RESOURCE_LIMIT;
		}
		entries = (sqlparser_view_bind_position_entry_t *)realloc(
			cache->entries,
			capacity * sizeof(*entries));
		if (entries == NULL) {
			sqlparser_error_set_message(
				out_error,
				SQLPARSER_STATUS_NO_MEMORY,
				"out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		cache->entries = entries;
		cache->capacity = capacity;
	}
	entry = &cache->entries[cache->count];
	memset(entry, 0, sizeof(*entry));
	entry->param_ref = param_ref;
	entry->traversal_index = cache->count;
	entry->statement_index = statement_index;
	entry->original_number = param_ref->number;
	cache->count++;
	status = sqlparser_view_bind_position_render_public(
		handle,
		statement_index,
		param_ref,
		&entry->public_sql,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	public_length = strlen(entry->public_sql);
	if (!sqlparser_bind_token_exact(
		    handle->dialect,
		    entry->public_sql,
		    public_length,
		    0U,
		    public_length,
		    NULL)) {
		free(entry->public_sql);
		entry->public_sql = NULL;
	}
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_view_bind_position_walk(
	sqlparser_handle_t *handle,
	size_t statement_index,
	ProtobufCMessage *message,
	sqlparser_view_bind_position_cache_t *cache,
	sqlparser_error_t *out_error)
{
	const ProtobufCMessageDescriptor *descriptor;
	const uint8_t *base;
	unsigned int field_index;

	if (message == NULL) {
		return SQLPARSER_STATUS_OK;
	}
	descriptor = message->descriptor;
	if (descriptor == &pg_query__param_ref__descriptor) {
		return sqlparser_view_bind_position_append(
			handle,
			statement_index,
			(PgQuery__ParamRef *)message,
			cache,
			out_error);
	}
	if (descriptor == &pg_query__multi_assign_ref__descriptor) {
		PgQuery__MultiAssignRef *multi_assign_ref;

		multi_assign_ref = (PgQuery__MultiAssignRef *)message;
		if (multi_assign_ref->colno > 1 &&
		    multi_assign_ref->ncolumns >= multi_assign_ref->colno) {
			return SQLPARSER_STATUS_OK;
		}
	}
	if (descriptor == NULL) {
		return SQLPARSER_STATUS_OK;
	}
	base = (const uint8_t *)message;
	for (field_index = 0U;
	     field_index < descriptor->n_fields;
	     field_index++) {
		const ProtobufCFieldDescriptor *field;

		field = &descriptor->fields[field_index];
		if (field->type != PROTOBUF_C_TYPE_MESSAGE) {
			continue;
		}
		if (field->label == PROTOBUF_C_LABEL_REPEATED) {
			ProtobufCMessage *const *items;
			size_t item_count;
			size_t item_index;

			item_count = *(const size_t *)(
				base + field->quantifier_offset);
			items = *(ProtobufCMessage *const * const *)(
				base + field->offset);
			for (item_index = 0U;
			     item_index < item_count;
			     item_index++) {
				sqlparser_status_t status;

				status = sqlparser_view_bind_position_walk(
					handle,
					statement_index,
					items[item_index],
					cache,
					out_error);
				if (status != SQLPARSER_STATUS_OK) {
					return status;
				}
			}
			continue;
		}
		if ((field->flags &
		     PROTOBUF_C_FIELD_FLAG_ONEOF) != 0U &&
		    *(const int *)(base + field->quantifier_offset) !=
			    (int)field->id) {
			continue;
		}
		{
			ProtobufCMessage *child;
			sqlparser_status_t status;

			child = *(ProtobufCMessage *const *)(
				base + field->offset);
			status = sqlparser_view_bind_position_walk(
				handle,
				statement_index,
				child,
				cache,
				out_error);
			if (status != SQLPARSER_STATUS_OK) {
				return status;
			}
		}
	}
	return SQLPARSER_STATUS_OK;
}

static int sqlparser_view_bind_pointer_entry_compare(
	const void *left,
	const void *right)
{
	const sqlparser_view_bind_pointer_entry_t *a;
	const sqlparser_view_bind_pointer_entry_t *b;

	a = (const sqlparser_view_bind_pointer_entry_t *)left;
	b = (const sqlparser_view_bind_pointer_entry_t *)right;
	if (a->pointer < b->pointer) {
		return -1;
	}
	if (a->pointer > b->pointer) {
		return 1;
	}
	return 0;
}

static sqlparser_status_t sqlparser_view_bind_position_collect(
	sqlparser_handle_t *handle,
	sqlparser_view_bind_position_cache_t *cache,
	sqlparser_error_t *out_error)
{
	size_t statement_index;

	for (statement_index = 0U;
	     statement_index < handle->statement_count;
	     statement_index++) {
		PgQuery__Node *statement;
		sqlparser_status_t status;

		statement = NULL;
		status = sqlparser_get_statement_node(
			handle,
			statement_index,
			&statement,
			out_error);
		if (status == SQLPARSER_STATUS_OK) {
			status = sqlparser_view_bind_position_walk(
				handle,
				statement_index,
				(ProtobufCMessage *)statement,
				cache,
				out_error);
		}
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
	}
	if (cache->count == 0U) {
		cache->built = 1;
		return SQLPARSER_STATUS_OK;
	}
	if (cache->count >
	    (size_t)-1 / sizeof(*cache->pointer_entries)) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_RESOURCE_LIMIT,
			"bind position pointer map is too large");
		return SQLPARSER_STATUS_RESOURCE_LIMIT;
	}
	cache->pointer_entries =
		(sqlparser_view_bind_pointer_entry_t *)malloc(
			cache->count * sizeof(*cache->pointer_entries));
	if (cache->pointer_entries == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_NO_MEMORY,
			"out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	for (statement_index = 0U;
	     statement_index < cache->count;
	     statement_index++) {
		cache->pointer_entries[statement_index].pointer =
			(uintptr_t)cache->entries[statement_index].param_ref;
		cache->pointer_entries[statement_index].entry_index =
			statement_index;
	}
	qsort(
		cache->pointer_entries,
		cache->count,
		sizeof(*cache->pointer_entries),
		sqlparser_view_bind_pointer_entry_compare);
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_view_bind_position_marker_is_raw(
	const sqlparser_handle_t *handle,
	int32_t marker,
	int *out_is_raw,
	sqlparser_error_t *out_error)
{
	char core_token[32];
	size_t statement_index;

	if (out_is_raw == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"bind marker probe output must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_is_raw = 0;
	(void)snprintf(
		core_token,
		sizeof(core_token),
		"$%ld",
		(long)marker);
	for (statement_index = 0U;
	     statement_index < handle->statement_count;
	     statement_index++) {
		char *public_token;
		sqlparser_status_t status;

		public_token = NULL;
		status = sqlparser_postprocess_handle_sql_fragment(
			handle,
			statement_index,
			core_token,
			SQLPARSER_FRAGMENT_CONTEXT_OPAQUE,
			NULL,
			0U,
			"bind marker probe",
			&public_token,
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			free(public_token);
			return status;
		}
		if (public_token == NULL ||
		    strcmp(public_token, core_token) != 0) {
			free(public_token);
			return SQLPARSER_STATUS_OK;
		}
		free(public_token);
	}
	*out_is_raw = 1;
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_view_bind_position_prepare_markers(
	const sqlparser_handle_t *handle,
	sqlparser_view_bind_position_cache_t *cache,
	sqlparser_error_t *out_error)
{
	int32_t marker;
	int32_t maximum_number;
	int marker_is_raw;
	size_t index;
	size_t public_count;
	int32_t start_limit;
	sqlparser_status_t status;

	maximum_number = 0;
	public_count = 0U;
	for (index = 0U; index < cache->count; index++) {
		if (cache->entries[index].original_number > maximum_number) {
			maximum_number =
				cache->entries[index].original_number;
		}
		if (cache->entries[index].public_sql != NULL) {
			public_count++;
		}
	}
	if (cache->count > (size_t)INT32_MAX) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_RESOURCE_LIMIT,
			"bind marker space is exhausted");
		return SQLPARSER_STATUS_RESOURCE_LIMIT;
	}
	if (public_count == 0U) {
		return SQLPARSER_STATUS_OK;
	}
	start_limit =
		INT32_MAX - (int32_t)cache->count + 1;
	if (maximum_number >= start_limit) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_RESOURCE_LIMIT,
			"bind marker space is exhausted");
		return SQLPARSER_STATUS_RESOURCE_LIMIT;
	}
	marker = maximum_number + 1;
	for (;;) {
		status = sqlparser_view_bind_position_marker_is_raw(
			handle,
			marker,
			&marker_is_raw,
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
		if (marker_is_raw) {
			break;
		}
		if (marker >= start_limit) {
			sqlparser_error_set_message(
				out_error,
				SQLPARSER_STATUS_RESOURCE_LIMIT,
				"bind marker space is exhausted");
			return SQLPARSER_STATUS_RESOURCE_LIMIT;
		}
		marker = marker <= start_limit / 2 ?
			marker * 2 :
			start_limit;
	}
	cache->marker_start = marker;
	for (index = 0U; index < cache->count; index++) {
		sqlparser_view_bind_position_entry_t *entry;

		entry = &cache->entries[index];
		if (entry->public_sql == NULL) {
			continue;
		}
		entry->marker_number =
			cache->marker_start + (int32_t)index;
	}
	return SQLPARSER_STATUS_OK;
}

static void sqlparser_view_bind_position_set_marker_numbers(
	sqlparser_view_bind_position_cache_t *cache,
	int use_markers)
{
	size_t index;

	for (index = 0U; index < cache->count; index++) {
		sqlparser_view_bind_position_entry_t *entry;

		entry = &cache->entries[index];
		entry->param_ref->number =
			use_markers && entry->marker_number > 0 ?
				entry->marker_number :
				entry->original_number;
	}
}

static sqlparser_status_t sqlparser_view_bind_position_pack_markers(
	sqlparser_handle_t *handle,
	sqlparser_view_bind_position_cache_t *cache,
	PgQueryProtobuf *out_tree,
	sqlparser_error_t *out_error)
{
	size_t packed_len;
	size_t packed_size;
	uint8_t *packed;

	if (out_tree == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"bind marker tree output must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	memset(out_tree, 0, sizeof(*out_tree));
	sqlparser_view_bind_position_set_marker_numbers(cache, 1);

	packed_size =
		pg_query__parse_result__get_packed_size(handle->ast);
	if (packed_size == 0U) {
		sqlparser_view_bind_position_set_marker_numbers(cache, 0);
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INTERNAL_ERROR,
			"failed to size bind marker tree");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}
	packed = (uint8_t *)malloc(packed_size);
	if (packed == NULL) {
		sqlparser_view_bind_position_set_marker_numbers(cache, 0);
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_NO_MEMORY,
			"out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	packed_len = pg_query__parse_result__pack(handle->ast, packed);
	sqlparser_view_bind_position_set_marker_numbers(cache, 0);
	if (packed_len != packed_size) {
		free(packed);
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INTERNAL_ERROR,
			"failed to pack bind marker tree");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}
	out_tree->data = (char *)packed;
	out_tree->len = packed_len;
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_view_bind_position_deparse_markers(
	const sqlparser_handle_t *handle,
	PgQueryProtobuf tree,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	PgQueryDeparseResult result;
	char *public_sql;
	sqlparser_status_t status;

	if (out_sql == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"bind marker SQL output must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_sql = NULL;
	sqlparser_pg_query_prepare();
	if (handle->surface_source_complete) {
		result = pg_query_deparse_protobuf(tree);
	} else {
		result = sqlparser_deparse_protobuf_for_handle(
			handle,
			tree,
			0U,
			0U,
			handle->parser_sql_len);
	}
	if (result.error != NULL || result.query == NULL) {
		pg_query_free_deparse_result(result);
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INTERNAL_ERROR,
			"failed to deparse bind marker tree");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}
	if (handle->dialect_ops == NULL ||
	    handle->dialect_ops->postprocess_deparse == NULL) {
		*out_sql = result.query;
		result.query = NULL;
		pg_query_free_deparse_result(result);
		return SQLPARSER_STATUS_OK;
	}
	public_sql = NULL;
	status = handle->dialect_ops->postprocess_deparse(
		result.query,
		handle->dialect_state,
		&public_sql,
		out_error);
	pg_query_free_deparse_result(result);
	if (status != SQLPARSER_STATUS_OK) {
		free(public_sql);
		return status;
	}
	*out_sql = public_sql;
	return SQLPARSER_STATUS_OK;
}

static size_t sqlparser_view_bind_position_marker_at(
	const sqlparser_view_bind_position_cache_t *cache,
	const char *sql,
	size_t position)
{
	unsigned long marker;
	size_t entry_index;
	size_t token_end;

	if (cache == NULL || sql == NULL || sql[position] != '$' ||
	    !isdigit((unsigned char)sql[position + 1U])) {
		return (size_t)-1;
	}
	marker = 0UL;
	token_end = position + 1U;
	while (isdigit((unsigned char)sql[token_end])) {
		unsigned int digit;

		digit = (unsigned int)(sql[token_end] - '0');
		if (marker > (ULONG_MAX - digit) / 10UL) {
			return (size_t)-1;
		}
		marker = marker * 10UL + digit;
		token_end++;
	}
	if (sqlparser_public_char_is_ident(
		    (unsigned char)sql[token_end])) {
		return (size_t)-1;
	}
	if (cache->marker_start <= 0 ||
	    marker < (unsigned long)cache->marker_start) {
		return (size_t)-1;
	}
	entry_index =
		(size_t)(marker - (unsigned long)cache->marker_start);
	if (entry_index < cache->count &&
	    cache->entries[entry_index].marker_number > 0 &&
	    marker ==
		    (unsigned long)cache->entries[entry_index].marker_number) {
		return entry_index;
	}
	return (size_t)-1;
}

static int sqlparser_view_bind_missing_entry_compare(
	const void *left,
	const void *right)
{
	const sqlparser_view_bind_missing_entry_t *a;
	const sqlparser_view_bind_missing_entry_t *b;
	int comparison;

	a = (const sqlparser_view_bind_missing_entry_t *)left;
	b = (const sqlparser_view_bind_missing_entry_t *)right;
	comparison = strcmp(a->public_sql, b->public_sql);
	if (comparison != 0) {
		return comparison;
	}
	if (a->statement_index < b->statement_index) {
		return -1;
	}
	if (a->statement_index > b->statement_index) {
		return 1;
	}
	if (a->original_number < b->original_number) {
		return -1;
	}
	if (a->original_number > b->original_number) {
		return 1;
	}
	if (a->traversal_index < b->traversal_index) {
		return -1;
	}
	if (a->traversal_index > b->traversal_index) {
		return 1;
	}
	return 0;
}

static int sqlparser_view_bind_public_token_compare(
	const char *public_sql,
	const char *sql,
	size_t start,
	size_t end)
{
	size_t common_length;
	size_t public_length;
	size_t token_length;
	int comparison;

	public_length = strlen(public_sql);
	token_length = end - start;
	common_length = public_length < token_length ?
		public_length :
		token_length;
	comparison = memcmp(public_sql, sql + start, common_length);
	if (comparison != 0) {
		return comparison;
	}
	if (public_length < token_length) {
		return -1;
	}
	if (public_length > token_length) {
		return 1;
	}
	return 0;
}

static sqlparser_status_t sqlparser_view_bind_position_assign(
	const sqlparser_handle_t *handle,
	sqlparser_view_bind_position_cache_t *cache,
	const char *sql,
	sqlparser_error_t *out_error)
{
	sqlparser_bind_role_cursor_t role_cursor;
	sqlparser_bind_scanner_t scanner;
	sqlparser_bind_token_t token;
	sqlparser_view_bind_missing_group_t *groups;
	sqlparser_view_bind_missing_entry_t *missing;
	size_t entry_index;
	size_t group_count;
	size_t missing_count;
	size_t missing_index;
	size_t missing_resolved;
	size_t position;

	position = 0U;
	sqlparser_bind_scanner_init_markers(
		&scanner,
		handle->dialect,
		sql,
		(size_t)cache->marker_start,
		cache->count);
	sqlparser_bind_role_cursor_init(&role_cursor, handle, sql);
	while (sqlparser_bind_scanner_next(&scanner, &token)) {
		size_t marker_index;

		marker_index = sqlparser_view_bind_position_marker_at(
			cache,
			sql,
			token.start);
		if (!sqlparser_bind_role_cursor_accept(&role_cursor, &token)) {
			if (marker_index != (size_t)-1 &&
			    cache->entries[marker_index].public_sql != NULL &&
			    strcmp(
				    cache->entries[marker_index].public_sql,
				    "?") == 0) {
				position++;
				cache->entries[marker_index].position = position;
			} else if (marker_index != (size_t)-1) {
				cache->entries[marker_index].position = SIZE_MAX;
			}
			continue;
		}
		position++;
		if (marker_index != (size_t)-1) {
			cache->entries[marker_index].position = position;
		}
	}

	missing_count = 0U;
	for (entry_index = 0U;
	     entry_index < cache->count;
	     entry_index++) {
		if (cache->entries[entry_index].public_sql != NULL &&
		    cache->entries[entry_index].position == 0U) {
			missing_count++;
		}
	}
	if (missing_count == 0U) {
		return SQLPARSER_STATUS_OK;
	}
	if (missing_count > (size_t)-1 / sizeof(*missing) ||
	    missing_count > (size_t)-1 / sizeof(*groups)) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_RESOURCE_LIMIT,
			"missing bind position map is too large");
		return SQLPARSER_STATUS_RESOURCE_LIMIT;
	}
	missing = missing_count > 0U ?
		(sqlparser_view_bind_missing_entry_t *)malloc(
			missing_count * sizeof(*missing)) :
		NULL;
	if (missing_count > 0U && missing == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_NO_MEMORY,
			"out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	groups = missing_count > 0U ?
		(sqlparser_view_bind_missing_group_t *)malloc(
			missing_count * sizeof(*groups)) :
		NULL;
	if (missing_count > 0U && groups == NULL) {
		free(missing);
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_NO_MEMORY,
			"out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	missing_index = 0U;
	for (entry_index = 0U;
	     entry_index < cache->count;
	     entry_index++) {
		sqlparser_view_bind_position_entry_t *entry;

		entry = &cache->entries[entry_index];
		if (entry->public_sql == NULL ||
		    entry->position != 0U) {
			continue;
		}
		missing[missing_index].entry_index = entry_index;
		missing[missing_index].public_sql = entry->public_sql;
		missing[missing_index].statement_index =
			entry->statement_index;
		missing[missing_index].original_number =
			entry->original_number;
		missing[missing_index].traversal_index =
			entry->traversal_index;
		missing_index++;
	}
	if (missing_count > 1U) {
		qsort(
			missing,
			missing_count,
			sizeof(*missing),
			sqlparser_view_bind_missing_entry_compare);
	}
	group_count = 0U;
	for (missing_index = 0U;
	     missing_index < missing_count;
	     missing_index++) {
		sqlparser_view_bind_missing_group_t *group;

		if (group_count == 0U ||
		    strcmp(
			    groups[group_count - 1U].public_sql,
			    missing[missing_index].public_sql) != 0) {
			group = &groups[group_count++];
			group->public_sql =
				missing[missing_index].public_sql;
			group->offset = missing_index;
			group->count = 0U;
			group->used = 0U;
		}
		groups[group_count - 1U].count++;
	}

	missing_resolved = 0U;
	position = 0U;
	sqlparser_bind_scanner_init_markers(
		&scanner,
		handle->dialect,
		sql,
		(size_t)cache->marker_start,
		cache->count);
	sqlparser_bind_role_cursor_init(&role_cursor, handle, sql);
	while (sqlparser_bind_scanner_next(&scanner, &token)) {
		size_t marker_index;
		int accepted;

		marker_index = sqlparser_view_bind_position_marker_at(
			cache,
			sql,
			token.start);
		accepted = sqlparser_bind_role_cursor_accept(&role_cursor, &token);
		if (!accepted &&
		    !(marker_index != (size_t)-1 &&
		      cache->entries[marker_index].public_sql != NULL &&
		      strcmp(
			      cache->entries[marker_index].public_sql,
			      "?") == 0)) {
			continue;
		}
		position++;
		if (marker_index != (size_t)-1) {
			continue;
		}
		if (group_count > 0U) {
			size_t high;
			size_t low;

			low = 0U;
			high = group_count;
			while (low < high) {
				size_t middle;
				int comparison;

				middle = low + (high - low) / 2U;
				comparison =
					sqlparser_view_bind_public_token_compare(
						groups[middle].public_sql,
						sql,
						token.start,
						token.end);
				if (comparison < 0) {
					low = middle + 1U;
				} else {
					high = middle;
				}
			}
			if (low < group_count &&
			    sqlparser_view_bind_public_token_compare(
				    groups[low].public_sql,
				    sql,
				    token.start,
				    token.end) == 0 &&
			    groups[low].used < groups[low].count) {
				sqlparser_view_bind_position_entry_t *entry;

				entry = &cache->entries[
					missing[
						groups[low].offset +
						groups[low].used]
						.entry_index];
				entry->position = position;
				groups[low].used++;
				missing_resolved++;
			}
		}
	}
	free(groups);
	free(missing);
	if (missing_resolved != missing_count) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INTERNAL_ERROR,
			"bind marker position could not be resolved");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_view_bind_position_cache_build(
	sqlparser_handle_t *handle,
	sqlparser_view_bind_position_cache_t *cache,
	sqlparser_error_t *out_error)
{
	PgQueryProtobuf tree;
	char *marker_sql;
	sqlparser_status_t status;

	if (handle == NULL || cache == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"bind position cache arguments are missing");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	if (cache->built) {
		return SQLPARSER_STATUS_OK;
	}
	status = sqlparser_handle_ensure_ast(handle, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	status = sqlparser_view_bind_position_collect(
		handle,
		cache,
		out_error);
	if (status != SQLPARSER_STATUS_OK || cache->built) {
		if (status != SQLPARSER_STATUS_OK) {
			sqlparser_view_bind_position_cache_release(cache);
		}
		return status;
	}
	status = sqlparser_view_bind_position_prepare_markers(
		handle,
		cache,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_view_bind_position_cache_release(cache);
		return status;
	}
	marker_sql = NULL;
	if (handle->control != NULL) {
		sqlparser_view_control_unit_span_t *spans;
		size_t unit_index;
		size_t unit_count;

		unit_count = handle->control->unit_count;
		if (unit_count > (size_t)-1 / sizeof(*spans)) {
			sqlparser_view_bind_position_cache_release(cache);
			sqlparser_error_set_message(
				out_error,
				SQLPARSER_STATUS_RESOURCE_LIMIT,
				"control bind position map is too large");
			return SQLPARSER_STATUS_RESOURCE_LIMIT;
		}
		spans = unit_count > 0U ?
			(sqlparser_view_control_unit_span_t *)malloc(
				unit_count * sizeof(*spans)) :
			NULL;
		if (unit_count > 0U && spans == NULL) {
			sqlparser_view_bind_position_cache_release(cache);
			sqlparser_error_set_message(
				out_error,
				SQLPARSER_STATUS_NO_MEMORY,
				"out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		for (unit_index = 0U; unit_index < unit_count; unit_index++) {
			spans[unit_index].offset =
				handle->control->units[unit_index].current_offset;
			spans[unit_index].length =
				handle->control->units[unit_index].current_length;
		}
		sqlparser_view_bind_position_set_marker_numbers(cache, 1);
		status = sqlparser_control_build_public_sql(
			handle,
			&marker_sql,
			out_error);
		sqlparser_view_bind_position_set_marker_numbers(cache, 0);
		for (unit_index = 0U; unit_index < unit_count; unit_index++) {
			handle->control->units[unit_index].current_offset =
				spans[unit_index].offset;
			handle->control->units[unit_index].current_length =
				spans[unit_index].length;
		}
		free(spans);
		if (status == SQLPARSER_STATUS_OK) {
			status = sqlparser_view_bind_position_assign(
				handle,
				cache,
				marker_sql,
				out_error);
		}
		free(marker_sql);
		if (status != SQLPARSER_STATUS_OK) {
			sqlparser_view_bind_position_cache_release(cache);
			return status;
		}
		cache->built = 1;
		return SQLPARSER_STATUS_OK;
	}
	memset(&tree, 0, sizeof(tree));
	status = sqlparser_view_bind_position_pack_markers(
		handle,
		cache,
		&tree,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_view_bind_position_cache_release(cache);
		return status;
	}
	status = sqlparser_view_bind_position_deparse_markers(
		handle,
		tree,
		&marker_sql,
		out_error);
	free(tree.data);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_view_bind_position_assign(
			handle,
			cache,
			marker_sql,
			out_error);
	}
	free(marker_sql);
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_view_bind_position_cache_release(cache);
		return status;
	}
	cache->built = 1;
	return SQLPARSER_STATUS_OK;
}

static int sqlparser_view_bind_info_set_position(
	sqlparser_handle_t *handle,
	sqlparser_view_bind_position_cache_t *cache,
	const PgQuery__Node *value_node,
	sqlparser_view_bind_info_t *out_info,
	sqlparser_error_t *out_error)
{
	uintptr_t pointer;
	size_t high;
	size_t low;
	sqlparser_status_t status;

	if (handle == NULL || cache == NULL ||
	    value_node == NULL || out_info == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"bind position arguments are missing");
		return -1;
	}
	value_node = sqlparser_unwrap_grouping_node_const(value_node);
	if (value_node == NULL ||
	    value_node->node_case != PG_QUERY__NODE__NODE_PARAM_REF ||
	    value_node->param_ref == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"bind position requires a ParamRef node");
		return -1;
	}
	status = sqlparser_view_bind_position_cache_build(
		handle,
		cache,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return -1;
	}
	pointer = (uintptr_t)value_node->param_ref;
	low = 0U;
	high = cache->count;
	while (low < high) {
		size_t entry_index;
		size_t middle;

		middle = low + (high - low) / 2U;
		if (cache->pointer_entries[middle].pointer < pointer) {
			low = middle + 1U;
			continue;
		}
		if (cache->pointer_entries[middle].pointer > pointer) {
			high = middle;
			continue;
		}
		entry_index =
			cache->pointer_entries[middle].entry_index;
		if (cache->entries[entry_index].position == SIZE_MAX) {
			return 0;
		}
		if (cache->entries[entry_index].position != 0U) {
			out_info->position =
				cache->entries[entry_index].position;
			return 1;
		}
		break;
	}
	sqlparser_error_set_message(
		out_error,
		SQLPARSER_STATUS_INTERNAL_ERROR,
		"bind position could not be resolved");
	return -1;
}

static int sqlparser_view_bind_info_from_value(
	sqlparser_handle_t *handle,
	sqlparser_view_bind_position_cache_t *position_cache,
	const char *public_sql,
	const PgQuery__Node *value_node,
	sqlparser_view_bind_info_t *out_info,
	sqlparser_error_t *out_error)
{
	char buffer[32];
	char *position_name;
	sqlparser_bind_token_t token;
	size_t public_length;
	int status;

	if (out_info == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "out_info must not be NULL");
		return -1;
	}
	memset(out_info, 0, sizeof(*out_info));
	out_info->kind = SQLPARSER_BIND_KIND_NONE;
	value_node = sqlparser_unwrap_grouping_node_const(value_node);
	if (public_sql == NULL ||
	    handle == NULL ||
	    value_node == NULL ||
	    value_node->node_case != PG_QUERY__NODE__NODE_PARAM_REF ||
	    value_node->param_ref == NULL) {
		return 0;
	}
	public_length = strlen(public_sql);
	if (!sqlparser_bind_token_exact(
		    handle->dialect,
		    public_sql,
		    public_length,
		    0U,
		    public_length,
		    &token)) {
		return 0;
	}

	if (token.key_length == 0U) {
		if (value_node->param_ref->number <= 0) {
			return 0;
		}
		out_info->kind = token.kind;
		status = sqlparser_view_bind_info_set_position(
			handle,
			position_cache,
			value_node,
			out_info,
			out_error);
		if (status <= 0) {
			sqlparser_view_bind_info_release(out_info);
			return status;
		}
		(void)snprintf(
			buffer,
			sizeof(buffer),
			"%lu",
			(unsigned long)out_info->position);
		position_name = sqlparser_strdup(buffer);
		if (position_name == NULL) {
			sqlparser_view_bind_info_release(out_info);
			sqlparser_error_set_message(
				out_error,
				SQLPARSER_STATUS_NO_MEMORY,
				"out of memory");
			return -1;
		}
		out_info->name = position_name;
		return status;
	}

	out_info->name = sqlparser_strndup(
		public_sql + token.key_start,
		token.key_length);
	if (out_info->name == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return -1;
	}
	out_info->kind = token.kind;
	status = sqlparser_view_bind_info_set_position(
		handle,
		position_cache,
		value_node,
		out_info,
		out_error);
	if (status <= 0) {
		sqlparser_view_bind_info_release(out_info);
	}
	return status;
}

static sqlparser_status_t sqlparser_view_render_param_ref_public_sql(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	const PgQuery__ParamRef *param_ref,
	char **out_public_sql,
	sqlparser_error_t *out_error)
{
	char core_sql[32];

	if (out_public_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "out_public_sql must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_public_sql = NULL;
	if (param_ref == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "ParamRef node is missing");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	if (param_ref->number <= 0) {
		*out_public_sql = sqlparser_strdup("?");
		if (*out_public_sql == NULL) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		return SQLPARSER_STATUS_OK;
	}
	(void)snprintf(core_sql, sizeof(core_sql), "$%ld", (long)param_ref->number);
	return sqlparser_postprocess_handle_sql_fragment(
		handle,
		statement_index,
		core_sql,
		SQLPARSER_FRAGMENT_CONTEXT_EXPRESSION,
		(ProtobufCMessage *const *)&param_ref,
		1U,
		"query graph value",
		out_public_sql,
		out_error);
}

static sqlparser_status_t sqlparser_view_render_value_node_public_sql(
	sqlparser_handle_t *handle,
	size_t statement_index,
	PgQuery__Node *value_node,
	char **out_public_sql,
	sqlparser_error_t *out_error)
{
	sqlparser_error_t render_error;
	sqlparser_status_t status;
	char *core_sql;
	PgQuery__Node *semantic_node;

	if (out_public_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "out_public_sql must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_public_sql = NULL;
	if (handle == NULL || value_node == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "value node is missing");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	semantic_node = sqlparser_unwrap_grouping_node(value_node);
	if (semantic_node != NULL &&
	    semantic_node->node_case == PG_QUERY__NODE__NODE_PARAM_REF &&
	    semantic_node->param_ref != NULL) {
		return sqlparser_view_render_param_ref_public_sql(
			handle, statement_index, semantic_node->param_ref, out_public_sql, out_error);
	}

	core_sql = NULL;
	memset(&render_error, 0, sizeof(render_error));
	status = sqlparser_render_update_assignment_node_sql(
		handle,
		value_node,
		&core_sql,
		&render_error);
	if (status != SQLPARSER_STATUS_OK) {
		if (status == SQLPARSER_STATUS_NO_MEMORY || status == SQLPARSER_STATUS_RESOURCE_LIMIT) {
			sqlparser_error_set_message(out_error, status, render_error.message);
		}
		return status;
	}
	if (semantic_node != NULL &&
	    semantic_node->node_case == PG_QUERY__NODE__NODE_A_CONST &&
	    semantic_node->a_const != NULL &&
	    handle->dialect_ops != NULL &&
	    handle->dialect_ops->postprocess_literal_fragment != NULL) {
		status = handle->dialect_ops->postprocess_literal_fragment(
			core_sql,
			handle->dialect_state,
			statement_index,
			semantic_node->a_const,
			out_public_sql,
			out_error);
	} else {
		status = sqlparser_postprocess_handle_sql_fragment(
			handle,
			statement_index,
			core_sql,
			SQLPARSER_FRAGMENT_CONTEXT_EXPRESSION,
			(ProtobufCMessage *const *)&value_node,
			1U,
			"query graph value",
			out_public_sql,
			out_error);
	}
	free(core_sql);
	return status;
}

static PgQuery__CommonTableExpr *sqlparser_view_find_cte(
	const sqlparser_handle_t *handle,
	PgQuery__WithClause *with_clause,
	const char *name,
	sqlparser_identifier_source_t name_source)
{
	size_t index;

	if (with_clause == NULL || name == NULL || name[0] == '\0') {
		return NULL;
	}
	for (index = 0U; index < with_clause->n_ctes; index++) {
		PgQuery__Node *node;

		node = with_clause->ctes[index];
		if (node != NULL &&
		    node->node_case == PG_QUERY__NODE__NODE_COMMON_TABLE_EXPR &&
		    node->common_table_expr != NULL &&
		    node->common_table_expr->ctename != NULL) {
			sqlparser_identifier_source_t cte_source;

			(void)sqlparser_identifier_component_source(
				handle,
				node->common_table_expr->location,
				0U,
				&cte_source,
				NULL);
			if (sqlparser_identifier_semantic_equal(
				    handle,
				    node->common_table_expr->ctename,
				    cte_source,
				    name,
				    name_source)) {
				return node->common_table_expr;
			}
		}
	}
	return NULL;
}

typedef struct {
	size_t dml_global_index;
	size_t result_offset;
	size_t result_count;
	size_t parent_dml_index;
	int has_parent;
} sqlparser_graph_dml_result_meta_t;

typedef struct {
	sqlparser_graph_dml_result_meta_t *metas;
	size_t meta_count;
	size_t meta_capacity;
	sqlparser_graph_dml_result_t *results;
	size_t result_count;
	size_t result_capacity;
	sqlparser_graph_dml_reference_t *references;
	size_t reference_count;
	size_t reference_capacity;
	char **owned_texts;
	size_t owned_text_count;
	size_t owned_text_capacity;
} sqlparser_graph_dml_result_cache_t;

typedef struct {
	sqlparser_graph_session_scope_t scope;
	sqlparser_graph_session_target_kind_t target_kind;
	size_t name_offset;
	int has_name;
	size_t value_offset;
	size_t value_count;
} sqlparser_graph_session_item_cache_t;

typedef struct {
	size_t name_offset;
	int has_name;
	sqlparser_graph_session_value_kind_t kind;
	size_t text_offset;
	int has_text;
	sqlparser_literal_kind_t literal_kind;
	size_t literal_text_offset;
	int has_literal_text;
	long long literal_integer;
	int literal_boolean;
	int literal_quoted_identifier;
	size_t bind_key_offset;
	int has_bind_key;
	sqlparser_bind_kind_t bind_kind;
	size_t bind_sql_offset;
	int has_bind_sql;
	size_t bind_position;
	int has_bind_position;
} sqlparser_graph_session_value_cache_t;

typedef struct {
	size_t bind_offset;
	size_t bind_sql_offset;
	size_t bind_position;
	sqlparser_bind_kind_t bind_kind;
} sqlparser_graph_bind_cache_t;

typedef union {
	sqlparser_literal_view_t literal;
	sqlparser_graph_bind_cache_t bind;
} sqlparser_graph_value_payload_t;

typedef struct {
	size_t source_target_index;
	size_t source_field_index;
} sqlparser_graph_dml_cell_field_cache_t;

typedef union {
	sqlparser_literal_view_t literal;
	sqlparser_graph_bind_cache_t bind;
	sqlparser_graph_dml_cell_field_cache_t field;
	size_t expression_sql_offset;
} sqlparser_graph_dml_cell_payload_t;

enum {
	SQLPARSER_GRAPH_DML_CELL_HAS_SOURCE_TARGET = 1U << 0,
	SQLPARSER_GRAPH_DML_CELL_HAS_SOURCE_FIELD = 1U << 1,
	SQLPARSER_GRAPH_DML_CELL_HAS_SELECTOR = 1U << 2,
	SQLPARSER_GRAPH_DML_CELL_HAS_BIND = 1U << 3,
	SQLPARSER_GRAPH_DML_CELL_HAS_BIND_SQL = 1U << 4,
	SQLPARSER_GRAPH_DML_CELL_HAS_BIND_POSITION = 1U << 5,
	SQLPARSER_GRAPH_DML_CELL_HAS_EXPRESSION_SQL = 1U << 6
};

typedef struct {
	sqlparser_graph_dml_cell_payload_t payload;
	size_t dml_index;
	size_t row_index;
	size_t column_ordinal;
	size_t selector_item_index;
	uint8_t kind;
	uint8_t selector_kind;
	uint8_t flags;
} sqlparser_graph_dml_cell_cache_t;

_Static_assert(
	sizeof(sqlparser_graph_dml_cell_cache_t) <= 80U,
	"query graph DML cell cache must remain compact");

enum {
	SQLPARSER_GRAPH_VALUE_HAS_FIELD = 1U << 0,
	SQLPARSER_GRAPH_VALUE_HAS_SOURCE_FIELD = 1U << 1,
	SQLPARSER_GRAPH_VALUE_HAS_SELECTOR = 1U << 2,
	SQLPARSER_GRAPH_VALUE_HAS_BIND = 1U << 3,
	SQLPARSER_GRAPH_VALUE_HAS_BIND_SQL = 1U << 4,
	SQLPARSER_GRAPH_VALUE_HAS_BIND_POSITION = 1U << 5
};

typedef struct {
	size_t value_offset;
	sqlparser_graph_value_payload_t payload;
	uint8_t kind;
	uint8_t flags;
} sqlparser_graph_like_escape_cache_t;

_Static_assert(
	sizeof(sqlparser_graph_like_escape_cache_t) <= 56U,
	"query graph LIKE escape cache must remain compact");

typedef struct {
	size_t block_index;
	const char *operator_name;
	size_t field_index;
	size_t source_field_index;
	size_t selector_item_index;
	sqlparser_graph_value_payload_t payload;
	uint8_t clause;
	uint8_t operator_kind;
	uint8_t field_match_kind;
	uint8_t kind;
	uint8_t flags;
} sqlparser_graph_value_cache_t;

_Static_assert(
	sizeof(sqlparser_graph_value_cache_t) <= 88U,
	"query graph value cache must remain compact");

_Static_assert(
	SQLPARSER_CLAUSE_KIND_CONNECT_BY <= UINT8_MAX &&
	SQLPARSER_GRAPH_OPERATOR_NOT_ILIKE <= UINT8_MAX &&
	SQLPARSER_GRAPH_FIELD_MATCH_EXPRESSION_FIELD <= UINT8_MAX &&
	SQLPARSER_GRAPH_VALUE_FIELD <= UINT8_MAX &&
	SQLPARSER_GRAPH_LIKE_ESCAPE_EXPRESSION <= UINT8_MAX &&
	SQLPARSER_SELECTOR_KIND_MERGE_DELETE_CONDITION <= UINT8_MAX,
	"query graph compact enums must fit in one byte");

enum {
	SQLPARSER_GRAPH_TARGET_HAS_FIELD = 1U << 0,
	SQLPARSER_GRAPH_TARGET_HAS_VALUE = 1U << 1,
	SQLPARSER_GRAPH_TARGET_HAS_SOURCE_BLOCK = 1U << 2,
	SQLPARSER_GRAPH_TARGET_HAS_SELECTOR = 1U << 3,
	SQLPARSER_GRAPH_TARGET_HAS_TARGET_LIST_SELECTOR = 1U << 4,
	SQLPARSER_GRAPH_TARGET_HAS_SINK_VALUE = 1U << 5
};

typedef struct {
	size_t block_index;
	size_t ordinal;
	const char *output_name;
	size_t field_index;
	size_t value_index;
	sqlparser_index_span_t star_relations;
	size_t source_block_index;
	size_t selector_item_index;
	size_t selector_row_index;
	size_t sink_value_index;
	sqlparser_graph_target_kind_t kind;
	sqlparser_selector_kind_t selector_kind;
	sqlparser_selector_kind_t target_list_selector_kind;
	uint32_t flags;
} sqlparser_graph_target_cache_t;

_Static_assert(
	sizeof(sqlparser_graph_target_cache_t) <= 104U,
	"query graph target cache must remain compact");

typedef struct {
	size_t root_block_index;
	int has_root_block;
	size_t block_offset;
	size_t block_count;
	size_t relation_offset;
	size_t relation_count;
	size_t target_offset;
	size_t target_count;
	size_t field_offset;
	size_t field_count;
	size_t value_offset;
	size_t value_count;
	size_t set_offset;
	size_t set_count;
	size_t predicate_offset;
	size_t predicate_count;
	size_t dml_offset;
	size_t dml_count;
	size_t dml_branch_offset;
	size_t dml_branch_count;
	size_t dml_column_offset;
	size_t dml_column_count;
	size_t dml_cell_offset;
	size_t dml_cell_count;
	size_t dml_assignment_offset;
	size_t dml_assignment_count;
	size_t dml_reference_offset;
	size_t dml_reference_count;
	sqlparser_graph_session_action_t session_action;
	size_t session_item_offset;
	size_t session_item_count;
	size_t session_value_offset;
	size_t session_value_count;
} sqlparser_statement_graph_t;

typedef struct {
	sqlparser_graph_merge_action_kind_t action_kind;
	sqlparser_graph_merge_match_kind_t match_kind;
	sqlparser_index_span_t assignments;
} sqlparser_graph_merge_branch_detail_t;

struct sqlparser_query_graph_cache {
	unsigned long generation;
	size_t statement_count;
	sqlparser_statement_graph_t *statements;
	size_t block_count;
	size_t block_capacity;
	sqlparser_graph_block_t *blocks;
	size_t relation_count;
	size_t relation_capacity;
	sqlparser_graph_relation_t *relations;
	size_t target_count;
	size_t target_capacity;
	sqlparser_graph_target_cache_t *targets;
	size_t field_count;
	size_t field_capacity;
	sqlparser_graph_field_t *fields;
	size_t value_count;
	size_t value_capacity;
	sqlparser_graph_value_cache_t *values;
	size_t value_text_length;
	size_t value_text_capacity;
	char *value_text;
	size_t like_escape_count;
	size_t like_escape_capacity;
	sqlparser_graph_like_escape_cache_t *like_escapes;
	size_t set_count;
	size_t set_capacity;
	sqlparser_graph_set_t *sets;
	size_t predicate_count;
	size_t predicate_capacity;
	sqlparser_graph_predicate_t *predicates;
	size_t dml_count;
	size_t dml_capacity;
	sqlparser_graph_dml_t *dml;
	size_t dml_branch_count;
	size_t dml_branch_capacity;
	sqlparser_graph_dml_branch_t *dml_branches;
	size_t merge_branch_detail_capacity;
	sqlparser_graph_merge_branch_detail_t *merge_branch_details;
	size_t dml_column_count;
	size_t dml_column_capacity;
	sqlparser_graph_dml_column_t *dml_columns;
	size_t dml_cell_count;
	size_t dml_cell_capacity;
	sqlparser_graph_dml_cell_cache_t *dml_cells;
	size_t dml_assignment_count;
	size_t dml_assignment_capacity;
	sqlparser_graph_dml_assignment_t *dml_assignments;
	sqlparser_graph_dml_result_cache_t *dml_results;
	size_t session_item_count;
	size_t session_item_capacity;
	sqlparser_graph_session_item_cache_t *session_items;
	size_t session_value_count;
	size_t session_value_capacity;
	sqlparser_graph_session_value_cache_t *session_values;
	size_t session_text_length;
	size_t session_text_capacity;
	char *session_text;
	size_t index_pool_count;
	size_t index_pool_capacity;
	size_t *index_pool;
};

_Static_assert(
	sizeof(struct sqlparser_query_graph_cache) <= 520U,
	"query graph cache must remain compact");

typedef struct sqlparser_graph_scope {
	struct sqlparser_graph_scope *parent;
	size_t block_index;
	PgQuery__WithClause *with_clause;
} sqlparser_graph_scope_t;

typedef struct {
	PgQuery__CommonTableExpr *cte;
	size_t block_index;
	size_t dml_index;
	int has_block;
	int has_dml_index;
	int building;
	int built;
} sqlparser_graph_cte_entry_t;

typedef struct {
	int building;
	size_t scope_block_index;
	size_t target_relation_index;
	int has_target_relation;
	unsigned int target_reference_kinds;
	size_t result_index;
	int has_result;
	const char *action_marker;
} sqlparser_graph_dml_result_build_state_t;

typedef struct {
	sqlparser_handle_t *handle;
	sqlparser_query_graph_cache_t *cache;
	sqlparser_view_bind_position_cache_t *bind_positions;
	sqlparser_statement_graph_t *statement;
	size_t statement_index;
	PgQuery__Node *statement_node;
	PgQuery__SelectStmt *dml_tail_select;
	const char *session_bind_source;
	size_t session_bind_source_length;
	size_t session_bind_source_start;
	sqlparser_graph_scope_t *scope;
	sqlparser_target_path_entry_t target_path[SQLPARSER_TARGET_PATH_CAPACITY];
	size_t target_path_count;
	int hierarchy_query_active;
	unsigned int hierarchy_prior_depth;
	int selector_cache_built;
	int selector_cache_failed;
	sqlparser_graph_pointer_index_t *node_indices;
	size_t node_index_count;
	size_t node_index_capacity;
	sqlparser_graph_pointer_index_t *relation_indices;
	size_t relation_index_count;
	size_t relation_index_capacity;
	sqlparser_graph_pointer_index_t *select_target_list_indices;
	size_t select_target_list_index_count;
	size_t select_target_list_index_capacity;
	sqlparser_graph_pointer_index_t *name_indices;
	size_t name_index_count;
	size_t name_index_capacity;
	sqlparser_graph_relation_identifier_t *relation_identifiers;
	size_t relation_identifier_count;
	size_t relation_identifier_capacity;
	sqlparser_identifier_source_t *target_identifiers;
	size_t target_identifier_count;
	size_t target_identifier_capacity;
	sqlparser_graph_cte_entry_t *cte_entries;
	size_t cte_entry_count;
	size_t cte_entry_capacity;
	size_t *cte_entry_slots;
	size_t cte_entry_slot_capacity;
	sqlparser_graph_cte_entry_t *registering_cte;
	PgQuery__SelectStmt *registering_cte_stmt;
	sqlparser_dialect_dml_result_dml_t *dml_inventory;
	unsigned char *dml_inventory_built;
	size_t dml_inventory_count;
	size_t claiming_dml_index;
	int has_claiming_dml_index;
	const sqlparser_identifier_origin_map_t *origins;
	sqlparser_view_expression_source_cache_t expression_source_cache;
	const char *dml_source_sql;
	int building_dml_result;
	size_t dml_result_scope_block_index;
	size_t dml_result_target_relation_index;
	int dml_result_has_target_relation;
	unsigned int dml_result_target_reference_kinds;
	size_t dml_result_index;
	int has_dml_result;
	const char *dml_result_action_marker;
	const PgQuery__RangeVar *duplicate_delete_target;
	size_t duplicate_delete_block_index;
	int skip_duplicate_delete_target;
	const PgQuery__FuncCall *mysql_dml_join_wrapper;
	int in_on_conflict_update;
	size_t on_conflict_target_block_index;
	sqlparser_graph_dml_assignment_t *rhs_capture_assignment;
	size_t rhs_capture_block_index;
	int collect_relation_bindings;
	const PgQuery__RangeVar *collect_relation;
	size_t collect_target_relation_index;
	int collect_has_target_relation;
	int collect_target_relation_ambiguous;
	PgQuery__ColumnRef **collected_column_refs;
	size_t collected_column_ref_count;
	size_t collected_column_ref_capacity;
	PgQuery__ResTarget **collected_assignment_targets;
	size_t collected_assignment_target_count;
	size_t collected_assignment_target_capacity;
} sqlparser_graph_build_t;

static void sqlparser_graph_dml_result_build_state_save(
	const sqlparser_graph_build_t *build,
	sqlparser_graph_dml_result_build_state_t *state)
{
	if (build == NULL || state == NULL) {
		return;
	}
	state->building = build->building_dml_result;
	state->scope_block_index = build->dml_result_scope_block_index;
	state->target_relation_index =
		build->dml_result_target_relation_index;
	state->has_target_relation = build->dml_result_has_target_relation;
	state->target_reference_kinds =
		build->dml_result_target_reference_kinds;
	state->result_index = build->dml_result_index;
	state->has_result = build->has_dml_result;
	state->action_marker = build->dml_result_action_marker;
}

static void sqlparser_graph_dml_result_build_state_restore(
	sqlparser_graph_build_t *build,
	const sqlparser_graph_dml_result_build_state_t *state)
{
	if (build == NULL || state == NULL) {
		return;
	}
	build->building_dml_result = state->building;
	build->dml_result_scope_block_index = state->scope_block_index;
	build->dml_result_target_relation_index =
		state->target_relation_index;
	build->dml_result_has_target_relation = state->has_target_relation;
	build->dml_result_target_reference_kinds =
		state->target_reference_kinds;
	build->dml_result_index = state->result_index;
	build->has_dml_result = state->has_result;
	build->dml_result_action_marker = state->action_marker;
}

static void sqlparser_graph_dml_result_build_state_suspend(
	sqlparser_graph_build_t *build,
	sqlparser_graph_dml_result_build_state_t *saved)
{
	sqlparser_graph_dml_result_build_state_t inactive;

	if (build == NULL || saved == NULL) {
		return;
	}
	sqlparser_graph_dml_result_build_state_save(build, saved);
	memset(&inactive, 0, sizeof(inactive));
	sqlparser_graph_dml_result_build_state_restore(build, &inactive);
}

static int sqlparser_graph_func_arg_is_non_field(
	const sqlparser_graph_build_t *build,
	const PgQuery__FuncCall *func_call,
	size_t arg_index)
{
	const char *name;
	size_t arg_count;

	if (arg_index != 0U || build == NULL || build->handle == NULL ||
	    !sqlparser_dialect_is_sqlserver_compatible(build->handle->dialect) ||
	    func_call == NULL || func_call->n_funcname != 1U ||
	    func_call->funcname == NULL) {
		return 0;
	}
	name = sqlparser_view_func_call_name(func_call);
	if (name == NULL) {
		return 0;
	}
	arg_count = func_call->n_args;
	if ((arg_count == 3U || arg_count == 4U) &&
	    sqlparser_text_equal_ci(name, "DATE_BUCKET")) {
		return 1;
	}
	if (arg_count == 3U &&
	    (sqlparser_text_equal_ci(name, "DATEADD") ||
	     sqlparser_text_equal_ci(name, "DATEDIFF") ||
	     sqlparser_text_equal_ci(name, "DATEDIFF_BIG"))) {
		return 1;
	}
	if (arg_count == 2U &&
	    (sqlparser_text_equal_ci(name, "DATENAME") ||
	     sqlparser_text_equal_ci(name, "DATEPART") ||
	     sqlparser_text_equal_ci(name, "DATETRUNC"))) {
		return 1;
	}
	return (arg_count == 1U || arg_count == 3U) &&
		sqlparser_text_equal_ci(name, "IDENTITY");
}

static int sqlparser_graph_selector_cache_append(
	sqlparser_graph_pointer_index_t **items,
	size_t *count,
	size_t *capacity,
	const void *pointer,
	sqlparser_error_t *out_error)
{
	sqlparser_graph_pointer_index_t *new_items;
	size_t new_capacity;

	if (items == NULL || count == NULL || capacity == NULL || pointer == NULL) {
		return 0;
	}
	if (*count >= *capacity) {
		new_capacity = *capacity != 0U ? *capacity : 8U;
		while (new_capacity <= *count) {
			if (new_capacity > ((size_t)-1) / 2U) {
				sqlparser_error_set_message(out_error, SQLPARSER_STATUS_RESOURCE_LIMIT, "query graph selector cache is too large");
				return -1;
			}
			new_capacity *= 2U;
		}
		if (new_capacity > ((size_t)-1) / sizeof(*new_items)) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_RESOURCE_LIMIT, "query graph selector cache is too large");
			return -1;
		}
		new_items = (sqlparser_graph_pointer_index_t *)realloc(*items, new_capacity * sizeof(*new_items));
		if (new_items == NULL) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return -1;
		}
		*items = new_items;
		*capacity = new_capacity;
	}
	(*items)[*count].pointer = pointer;
	(*items)[*count].index = *count;
	(*count)++;
	return 0;
}

static size_t sqlparser_graph_selector_cache_find(
	const sqlparser_graph_pointer_index_t *items,
	size_t count,
	const void *pointer)
{
	size_t index;

	if (items == NULL || pointer == NULL) {
		return (size_t)-1;
	}
	for (index = 0U; index < count; index++) {
		if (items[index].pointer == pointer) {
			return items[index].index;
		}
	}
	return (size_t)-1;
}

static void sqlparser_graph_selector_cache_clear(sqlparser_graph_build_t *build)
{
	if (build == NULL) {
		return;
	}
	free(build->node_indices);
	free(build->relation_indices);
	free(build->select_target_list_indices);
	free(build->name_indices);
	build->node_indices = NULL;
	build->relation_indices = NULL;
	build->select_target_list_indices = NULL;
	build->name_indices = NULL;
	build->node_index_count = 0U;
	build->relation_index_count = 0U;
	build->select_target_list_index_count = 0U;
	build->name_index_count = 0U;
	build->node_index_capacity = 0U;
	build->relation_index_capacity = 0U;
	build->select_target_list_index_capacity = 0U;
	build->name_index_capacity = 0U;
	build->selector_cache_built = 0;
	build->selector_cache_failed = 0;
}

static void sqlparser_graph_build_clear(sqlparser_graph_build_t *build)
{
	if (build == NULL) {
		return;
	}
	sqlparser_graph_selector_cache_clear(build);
	free(build->relation_identifiers);
	free(build->target_identifiers);
	free(build->cte_entries);
	free(build->cte_entry_slots);
	free(build->dml_inventory);
	free(build->dml_inventory_built);
	free(build->collected_column_refs);
	free(build->collected_assignment_targets);
	build->relation_identifiers = NULL;
	build->relation_identifier_count = 0U;
	build->relation_identifier_capacity = 0U;
	build->target_identifiers = NULL;
	build->target_identifier_count = 0U;
	build->target_identifier_capacity = 0U;
	build->cte_entries = NULL;
	build->cte_entry_count = 0U;
	build->cte_entry_capacity = 0U;
	build->cte_entry_slots = NULL;
	build->cte_entry_slot_capacity = 0U;
	build->registering_cte = NULL;
	build->registering_cte_stmt = NULL;
	build->dml_inventory = NULL;
	build->dml_inventory_built = NULL;
	build->dml_inventory_count = 0U;
	build->claiming_dml_index = 0U;
	build->has_claiming_dml_index = 0;
	build->origins = NULL;
	build->collected_column_refs = NULL;
	build->collected_column_ref_count = 0U;
	build->collected_column_ref_capacity = 0U;
	build->collected_assignment_targets = NULL;
	build->collected_assignment_target_count = 0U;
	build->collected_assignment_target_capacity = 0U;
}

static int sqlparser_graph_select_has_target_list(const PgQuery__SelectStmt *stmt)
{
	return stmt != NULL && stmt->n_target_list > 0U && stmt->target_list != NULL;
}

static int sqlparser_graph_selector_cache_record_name(
	sqlparser_graph_build_t *build,
	ProtobufCMessage *message,
	const sqlparser_name_context_t *context,
	const ProtobufCMessageDescriptor *descriptor,
	const ProtobufCFieldDescriptor *field,
	char **slot,
	sqlparser_error_t *out_error)
{
	if (slot == NULL || *slot == NULL || (*slot)[0] == '\0') {
		return 0;
	}
	if (!sqlparser_name_atom_is_identifier(
		    message,
		    context,
		    descriptor,
		    field)) {
		return 0;
	}
	return sqlparser_graph_selector_cache_append(
		&build->name_indices,
		&build->name_index_count,
		&build->name_index_capacity,
		slot,
		out_error);
}

static int sqlparser_graph_selector_cache_collect(
	sqlparser_graph_build_t *build,
	ProtobufCMessage *message,
	const sqlparser_name_context_t *context,
	sqlparser_error_t *out_error)
{
	const ProtobufCMessageDescriptor *descriptor;
	uint8_t *base;
	unsigned index;

	if (build == NULL || message == NULL) {
		return 0;
	}
	descriptor = message->descriptor;
	if (descriptor == NULL) {
		return 0;
	}
	if (descriptor == &pg_query__range_var__descriptor &&
	    sqlparser_graph_selector_cache_append(
		    &build->relation_indices,
		    &build->relation_index_count,
		    &build->relation_index_capacity,
		    message,
		    out_error) != 0) {
		return -1;
	}
	if (descriptor == &pg_query__select_stmt__descriptor &&
	    sqlparser_graph_select_has_target_list((PgQuery__SelectStmt *)message) &&
	    sqlparser_graph_selector_cache_append(
		    &build->select_target_list_indices,
		    &build->select_target_list_index_count,
		    &build->select_target_list_index_capacity,
		    message,
		    out_error) != 0) {
		return -1;
	}
	base = (uint8_t *)message;
	for (index = 0U; index < descriptor->n_fields; index++) {
		const ProtobufCFieldDescriptor *field;

		field = &descriptor->fields[index];
		if ((field->flags & PROTOBUF_C_FIELD_FLAG_ONEOF) != 0U) {
			const int case_value = *(const int *)(base + field->quantifier_offset);

			if (case_value != (int)field->id) {
				continue;
			}
		}

		if (field->type == PROTOBUF_C_TYPE_STRING) {
			if (field->label == PROTOBUF_C_LABEL_REPEATED) {
				size_t item_count;
				char **items;
				size_t item_index;

				item_count = *(const size_t *)(base + field->quantifier_offset);
				items = *(char ***)(base + field->offset);
				for (item_index = 0U; item_index < item_count; item_index++) {
					if (sqlparser_graph_selector_cache_record_name(
						    build,
						    message,
						    context,
						    descriptor,
						    field,
						    items != NULL ? &items[item_index] : NULL,
						    out_error) != 0) {
						return -1;
					}
				}
			} else if (sqlparser_graph_selector_cache_record_name(
					   build,
					   message,
					   context,
					   descriptor,
					   field,
					   (char **)(base + field->offset),
					   out_error) != 0) {
				return -1;
			}
			continue;
		}

		if (field->type == PROTOBUF_C_TYPE_MESSAGE) {
			sqlparser_name_context_t next_context;

			next_context =
				sqlparser_next_name_context(
					message,
					descriptor,
					field,
					context);
			if (field->label == PROTOBUF_C_LABEL_REPEATED) {
				size_t item_count;
				ProtobufCMessage **items;
				size_t item_index;

				item_count = *(const size_t *)(base + field->quantifier_offset);
				items = *(ProtobufCMessage ***)(base + field->offset);
				for (item_index = 0U; item_index < item_count; item_index++) {
					ProtobufCMessage *child;

					child = items != NULL ? items[item_index] : NULL;
					if (child == NULL) {
						continue;
					}
					if (child->descriptor == &pg_query__node__descriptor &&
					    !sqlparser_node_is_grouping_wrapper(
						    (PgQuery__Node *)child) &&
					    !sqlparser_node_is_mysql_dml_tail_wrapper(
						    (PgQuery__Node *)child,
						    build->dml_tail_select) &&
					    sqlparser_graph_selector_cache_append(
						    &build->node_indices,
						    &build->node_index_count,
						    &build->node_index_capacity,
						    child,
						    out_error) != 0) {
						return -1;
					}
					if (sqlparser_graph_selector_cache_collect(build, child, &next_context, out_error) != 0) {
						return -1;
					}
				}
			} else {
				ProtobufCMessage *child;

				child = *(ProtobufCMessage **)(base + field->offset);
				if (child == NULL) {
					continue;
				}
				if (child->descriptor == &pg_query__node__descriptor &&
				    !sqlparser_node_is_grouping_wrapper(
					    (PgQuery__Node *)child) &&
				    !sqlparser_node_is_mysql_dml_tail_wrapper(
					    (PgQuery__Node *)child,
					    build->dml_tail_select) &&
				    sqlparser_graph_selector_cache_append(
					    &build->node_indices,
					    &build->node_index_count,
					    &build->node_index_capacity,
					    child,
					    out_error) != 0) {
					return -1;
				}
				if (sqlparser_graph_selector_cache_collect(build, child, &next_context, out_error) != 0) {
					return -1;
				}
			}
		}
	}
	return 0;
}

static int sqlparser_graph_ensure_selector_cache(
	sqlparser_graph_build_t *build,
	sqlparser_error_t *out_error)
{
	if (build == NULL || build->statement_node == NULL) {
		return -1;
	}
	if (build->selector_cache_built) {
		return build->selector_cache_failed ? -1 : 0;
	}
	build->selector_cache_built = 1;
	if (sqlparser_graph_selector_cache_collect(build, (ProtobufCMessage *)build->statement_node, NULL, out_error) != 0) {
		sqlparser_graph_selector_cache_clear(build);
		build->selector_cache_built = 1;
		build->selector_cache_failed = 1;
		return -1;
	}
	return 0;
}

static size_t sqlparser_graph_find_cached_value_index(
	sqlparser_graph_build_t *build,
	PgQuery__Node *node)
{
	sqlparser_view_build_t fallback_build;

	if (node == NULL) {
		return (size_t)-1;
	}
	if (sqlparser_graph_ensure_selector_cache(build, NULL) == 0) {
		return sqlparser_graph_selector_cache_find(
			build->node_indices,
			build->node_index_count,
			node);
	}
	memset(&fallback_build, 0, sizeof(fallback_build));
	fallback_build.handle = build != NULL ? build->handle : NULL;
	fallback_build.statement_index = build != NULL ? build->statement_index : 0U;
	return sqlparser_view_find_value_index(&fallback_build, node);
}

static size_t sqlparser_graph_find_cached_name_index(
	sqlparser_graph_build_t *build,
	char **slot)
{
	sqlparser_view_build_t fallback_build;

	if (slot == NULL) {
		return (size_t)-1;
	}
	if (sqlparser_graph_ensure_selector_cache(build, NULL) == 0) {
		return sqlparser_graph_selector_cache_find(
			build->name_indices,
			build->name_index_count,
			slot);
	}
	memset(&fallback_build, 0, sizeof(fallback_build));
	fallback_build.handle = build != NULL ? build->handle : NULL;
	fallback_build.statement_index = build != NULL ? build->statement_index : 0U;
	return sqlparser_view_find_name_selector_index(&fallback_build, slot);
}

static size_t sqlparser_graph_find_cached_select_target_list_index(
	sqlparser_graph_build_t *build,
	PgQuery__SelectStmt *stmt)
{
	size_t index;

	if (stmt == NULL || !sqlparser_graph_select_has_target_list(stmt)) {
		return (size_t)-1;
	}
	if (sqlparser_graph_ensure_selector_cache(build, NULL) == 0) {
		return sqlparser_graph_selector_cache_find(
			build->select_target_list_indices,
			build->select_target_list_index_count,
			stmt);
	}
	if (build == NULL ||
	    sqlparser_find_select_target_list_index_by_stmt(
		    build->handle,
		    build->statement_index,
		    stmt,
		    &index,
		    NULL) != SQLPARSER_STATUS_OK) {
		return (size_t)-1;
	}
	return index;
}

static int sqlparser_graph_value_from_node(
	sqlparser_graph_build_t *build,
	size_t block_index,
	sqlparser_clause_kind_t clause,
	const char *operator_name,
	size_t field_index,
	int has_field,
	sqlparser_graph_field_match_kind_t field_match_kind,
	PgQuery__Node *value_node,
	const sqlparser_graph_like_escape_t *like_escape,
	sqlparser_graph_value_t *out_value,
	sqlparser_error_t *out_error);

static int sqlparser_query_graph_reserve_array_with_initial(
	void **items,
	size_t *capacity,
	size_t required,
	size_t item_size,
	size_t initial_capacity,
	sqlparser_error_t *out_error)
{
	void *new_items;
	size_t new_capacity;

	if (items == NULL || capacity == NULL || item_size == 0U || initial_capacity == 0U) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "invalid graph array");
		return -1;
	}
	if (required <= *capacity) {
		return 0;
	}
	new_capacity = *capacity != 0U ? *capacity : initial_capacity;
	while (new_capacity < required) {
		if (new_capacity > ((size_t)-1) / 2U) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_RESOURCE_LIMIT, "query graph is too large");
			return -1;
		}
		new_capacity *= 2U;
	}
	if (new_capacity > ((size_t)-1) / item_size) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_RESOURCE_LIMIT, "query graph is too large");
		return -1;
	}
	new_items = realloc(*items, new_capacity * item_size);
	if (new_items == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return -1;
	}
	*items = new_items;
	*capacity = new_capacity;
	return 0;
}

static int sqlparser_query_graph_reserve_array(
	void **items,
	size_t *capacity,
	size_t required,
	size_t item_size,
	sqlparser_error_t *out_error)
{
	return sqlparser_query_graph_reserve_array_with_initial(
		items,
		capacity,
		required,
		item_size,
		16U,
		out_error);
}

static int sqlparser_query_graph_reserve_sparse_array(
	void **items,
	size_t *capacity,
	size_t required,
	size_t item_size,
	sqlparser_error_t *out_error)
{
	return sqlparser_query_graph_reserve_array_with_initial(
		items,
		capacity,
		required,
		item_size,
		4U,
		out_error);
}

static void sqlparser_query_graph_shrink_array(
	void **items,
	size_t *capacity,
	size_t count,
	size_t item_size)
{
	void *new_items;

	if (items == NULL || capacity == NULL || item_size == 0U ||
	    *capacity <= count) {
		return;
	}
	if (count == 0U) {
		free(*items);
		*items = NULL;
		*capacity = 0U;
		return;
	}
	if (count > SIZE_MAX / item_size) {
		return;
	}
	new_items = realloc(*items, count * item_size);
	if (new_items != NULL) {
		*items = new_items;
		*capacity = count;
	}
}

static int sqlparser_graph_value_store_text(
	sqlparser_query_graph_cache_t *cache,
	const char *text,
	size_t *out_offset,
	sqlparser_error_t *out_error)
{
	size_t length;
	size_t required;

	if (cache == NULL || text == NULL || out_offset == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "invalid query graph value text");
		return -1;
	}
	length = strlen(text);
	if (length == SIZE_MAX ||
	    cache->value_text_length > SIZE_MAX - length - 1U) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_RESOURCE_LIMIT, "query graph value text is too large");
		return -1;
	}
	required = cache->value_text_length + length + 1U;
	if (sqlparser_query_graph_reserve_array_with_initial(
		    (void **)&cache->value_text,
		    &cache->value_text_capacity,
		    required,
		    sizeof(*cache->value_text),
		    128U,
		    out_error) != 0) {
		return -1;
	}
	*out_offset = cache->value_text_length;
	memcpy(cache->value_text + cache->value_text_length, text, length + 1U);
	cache->value_text_length = required;
	return 0;
}

static int sqlparser_graph_value_cache_init(
	sqlparser_query_graph_cache_t *cache,
	sqlparser_graph_value_cache_t *value,
	const sqlparser_graph_value_t *source,
	sqlparser_error_t *out_error)
{
	size_t text_length;

	if (cache == NULL || value == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "invalid query graph value cache");
		return -1;
	}
	memset(value, 0, sizeof(*value));
	value->block_index = source->block_index;
	value->operator_name = source->operator_name;
	value->field_index = source->field_index;
	value->source_field_index = source->source_field_index;
	value->selector_item_index = source->selector.item_index;
	value->clause = (uint8_t)source->clause;
	value->operator_kind = (uint8_t)source->operator_kind;
	value->field_match_kind = (uint8_t)source->field_match_kind;
	value->kind = (uint8_t)source->kind;
	if (source->has_field) {
		value->flags |= SQLPARSER_GRAPH_VALUE_HAS_FIELD;
	}
	if (source->has_source_field) {
		value->flags |= SQLPARSER_GRAPH_VALUE_HAS_SOURCE_FIELD;
	}
	if (source->has_selector) {
		value->flags |= SQLPARSER_GRAPH_VALUE_HAS_SELECTOR;
	}
	if (source->kind == SQLPARSER_GRAPH_VALUE_LITERAL) {
		value->payload.literal = source->literal;
	}
	if (source->kind != SQLPARSER_GRAPH_VALUE_BIND) {
		return 0;
	}
	value->payload.bind.bind_kind = source->bind_kind;
	value->payload.bind.bind_position = source->bind_position;
	if (source->has_bind) {
		value->flags |= SQLPARSER_GRAPH_VALUE_HAS_BIND;
	}
	if (source->has_bind_sql) {
		value->flags |= SQLPARSER_GRAPH_VALUE_HAS_BIND_SQL;
	}
	if (source->has_bind_position) {
		value->flags |= SQLPARSER_GRAPH_VALUE_HAS_BIND_POSITION;
	}
	text_length = cache->value_text_length;
	if ((source->has_bind &&
	     sqlparser_graph_value_store_text(
		     cache,
		     source->bind,
		     &value->payload.bind.bind_offset,
		     out_error) != 0) ||
	    (source->has_bind_sql &&
	     sqlparser_graph_value_store_text(
		     cache,
		     source->bind_sql,
		     &value->payload.bind.bind_sql_offset,
		     out_error) != 0)) {
		cache->value_text_length = text_length;
		return -1;
	}
	return 0;
}

static int sqlparser_graph_like_escape_cache_init(
	sqlparser_query_graph_cache_t *cache,
	sqlparser_graph_like_escape_cache_t *escape,
	size_t value_offset,
	const sqlparser_graph_like_escape_t *source,
	sqlparser_error_t *out_error)
{
	size_t text_length;

	if (cache == NULL || escape == NULL || source == NULL ||
	    source->kind == SQLPARSER_GRAPH_LIKE_ESCAPE_NONE) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "invalid query graph LIKE escape cache");
		return -1;
	}
	memset(escape, 0, sizeof(*escape));
	escape->value_offset = value_offset;
	escape->kind = (uint8_t)source->kind;
	if (source->kind == SQLPARSER_GRAPH_LIKE_ESCAPE_LITERAL) {
		escape->payload.literal = source->literal;
		return 0;
	}
	if (source->kind != SQLPARSER_GRAPH_LIKE_ESCAPE_BIND) {
		return 0;
	}
	escape->payload.bind.bind_kind = source->bind_kind;
	escape->payload.bind.bind_position = source->bind_position;
	if (source->has_bind) {
		escape->flags |= SQLPARSER_GRAPH_VALUE_HAS_BIND;
	}
	if (source->has_bind_sql) {
		escape->flags |= SQLPARSER_GRAPH_VALUE_HAS_BIND_SQL;
	}
	if (source->has_bind_position) {
		escape->flags |= SQLPARSER_GRAPH_VALUE_HAS_BIND_POSITION;
	}
	text_length = cache->value_text_length;
	if ((source->has_bind &&
	     sqlparser_graph_value_store_text(
		     cache,
		     source->bind,
		     &escape->payload.bind.bind_offset,
		     out_error) != 0) ||
	    (source->has_bind_sql &&
	     sqlparser_graph_value_store_text(
		     cache,
		     source->bind_sql,
		     &escape->payload.bind.bind_sql_offset,
		     out_error) != 0)) {
		cache->value_text_length = text_length;
		return -1;
	}
	return 0;
}

static const char *sqlparser_graph_value_text_at(
	const sqlparser_query_graph_cache_t *cache,
	size_t offset,
	sqlparser_error_t *out_error)
{
	if (cache == NULL || cache->value_text == NULL ||
	    offset >= cache->value_text_length ||
	    memchr(cache->value_text + offset, '\0', cache->value_text_length - offset) == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "query graph value text is invalid");
		return NULL;
	}
	return cache->value_text + offset;
}

static int sqlparser_graph_value_cache_copy_public(
	const sqlparser_query_graph_cache_t *cache,
	const sqlparser_graph_value_cache_t *source,
	size_t global_index,
	size_t statement_index,
	size_t value_index,
	sqlparser_graph_value_t *value,
	sqlparser_error_t *out_error)
{
	const sqlparser_graph_like_escape_cache_t *escape;
	const char *text;
	size_t begin;
	size_t end;
	size_t middle;

	if (cache == NULL || source == NULL || value == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "invalid query graph value cache");
		return -1;
	}
	if (global_index >= cache->value_count ||
	    cache->like_escape_count > cache->value_count ||
	    (cache->like_escape_count != 0U &&
	     cache->like_escapes == NULL)) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "query graph LIKE escape cache is invalid");
		return -1;
	}
	memset(value, 0, sizeof(*value));
	value->index = value_index;
	value->statement_index = statement_index;
	value->block_index = source->block_index;
	value->clause = (sqlparser_clause_kind_t)source->clause;
	value->operator_name = source->operator_name;
	value->operator_kind = (sqlparser_graph_operator_kind_t)source->operator_kind;
	value->field_index = source->field_index;
	value->has_field = (source->flags & SQLPARSER_GRAPH_VALUE_HAS_FIELD) != 0U;
	value->source_field_index = source->source_field_index;
	value->has_source_field =
		(source->flags & SQLPARSER_GRAPH_VALUE_HAS_SOURCE_FIELD) != 0U;
	value->field_match_kind =
		(sqlparser_graph_field_match_kind_t)source->field_match_kind;
	value->kind = (sqlparser_graph_value_kind_t)source->kind;
	if (value->kind == SQLPARSER_GRAPH_VALUE_LITERAL) {
		value->literal = source->payload.literal;
	}
	if (value->kind == SQLPARSER_GRAPH_VALUE_BIND) {
		value->has_bind =
			(source->flags & SQLPARSER_GRAPH_VALUE_HAS_BIND) != 0U;
		value->bind_kind = source->payload.bind.bind_kind;
		value->has_bind_sql =
			(source->flags & SQLPARSER_GRAPH_VALUE_HAS_BIND_SQL) != 0U;
		value->bind_position = source->payload.bind.bind_position;
		value->has_bind_position =
			(source->flags & SQLPARSER_GRAPH_VALUE_HAS_BIND_POSITION) != 0U;
	}
	value->has_selector =
		(source->flags & SQLPARSER_GRAPH_VALUE_HAS_SELECTOR) != 0U;
	if (value->has_selector) {
		value->selector.kind = SQLPARSER_SELECTOR_KIND_VALUE;
		value->selector.statement_index = statement_index;
		value->selector.item_index = source->selector_item_index;
	}
	if (value->has_bind) {
		text = sqlparser_graph_value_text_at(
			cache,
			source->payload.bind.bind_offset,
			out_error);
		if (text == NULL) {
			return -1;
		}
		sqlparser_view_copy_public_text(value->bind, sizeof(value->bind), text, NULL);
	}
	if (value->has_bind_sql) {
		text = sqlparser_graph_value_text_at(
			cache,
			source->payload.bind.bind_sql_offset,
			out_error);
		if (text == NULL) {
			return -1;
		}
		sqlparser_view_copy_public_text(value->bind_sql, sizeof(value->bind_sql), text, NULL);
	}
	escape = NULL;
	begin = 0U;
	end = cache->like_escape_count;
	while (begin < end) {
		middle = begin + (end - begin) / 2U;
		if (cache->like_escapes[middle].value_offset < global_index) {
			begin = middle + 1U;
		} else {
			end = middle;
		}
	}
	if (begin < cache->like_escape_count &&
	    cache->like_escapes[begin].value_offset == global_index) {
		escape = &cache->like_escapes[begin];
	}
	if (escape == NULL) {
		return 0;
	}
	value->like_escape.kind =
		(sqlparser_graph_like_escape_kind_t)escape->kind;
	if (value->like_escape.kind == SQLPARSER_GRAPH_LIKE_ESCAPE_LITERAL) {
		value->like_escape.literal = escape->payload.literal;
		return 0;
	}
	if (value->like_escape.kind != SQLPARSER_GRAPH_LIKE_ESCAPE_BIND) {
		return 0;
	}
	value->like_escape.has_bind =
		(escape->flags & SQLPARSER_GRAPH_VALUE_HAS_BIND) != 0U;
	value->like_escape.bind_kind = escape->payload.bind.bind_kind;
	value->like_escape.has_bind_sql =
		(escape->flags & SQLPARSER_GRAPH_VALUE_HAS_BIND_SQL) != 0U;
	value->like_escape.bind_position = escape->payload.bind.bind_position;
	value->like_escape.has_bind_position =
		(escape->flags & SQLPARSER_GRAPH_VALUE_HAS_BIND_POSITION) != 0U;
	if (value->like_escape.has_bind) {
		text = sqlparser_graph_value_text_at(
			cache,
			escape->payload.bind.bind_offset,
			out_error);
		if (text == NULL) {
			return -1;
		}
		sqlparser_view_copy_public_text(
			value->like_escape.bind,
			sizeof(value->like_escape.bind),
			text,
			NULL);
	}
	if (value->like_escape.has_bind_sql) {
		text = sqlparser_graph_value_text_at(
			cache,
			escape->payload.bind.bind_sql_offset,
			out_error);
		if (text == NULL) {
			return -1;
		}
		sqlparser_view_copy_public_text(
			value->like_escape.bind_sql,
			sizeof(value->like_escape.bind_sql),
			text,
			NULL);
	}
	return 0;
}

static int sqlparser_graph_dml_cell_cache_init(
	sqlparser_query_graph_cache_t *cache,
	sqlparser_graph_dml_cell_cache_t *cell,
	const sqlparser_graph_dml_cell_t *source,
	const char *expression_sql,
	size_t statement_index,
	sqlparser_error_t *out_error)
{
	size_t text_length;

	if (cache == NULL || cell == NULL || source == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "invalid query graph DML cell cache");
		return -1;
	}
	memset(cell, 0, sizeof(*cell));
	cell->dml_index = source->dml_index;
	cell->row_index = source->row_index;
	cell->column_ordinal = source->column_ordinal;
	cell->kind = (uint8_t)source->kind;
	if (source->has_selector) {
		if (source->selector.statement_index != statement_index) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "query graph DML cell selector statement is invalid");
			return -1;
		}
		switch (source->selector.kind) {
		case SQLPARSER_SELECTOR_KIND_VALUE:
			if (source->selector.row_index != 0U ||
			    source->selector.column_index != 0U ||
			    (source->kind != SQLPARSER_GRAPH_VALUE_LITERAL &&
			     source->kind != SQLPARSER_GRAPH_VALUE_BIND &&
			     source->kind != SQLPARSER_GRAPH_VALUE_DEFAULT)) {
				sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "query graph DML value selector is invalid");
				return -1;
			}
			cell->selector_item_index = source->selector.item_index;
			break;
		case SQLPARSER_SELECTOR_KIND_INSERT_CELL:
			if (source->selector.item_index != 0U ||
			    source->selector.row_index != source->row_index ||
			    source->selector.column_index != source->column_ordinal) {
				sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "query graph INSERT cell selector is invalid");
				return -1;
			}
			break;
		case SQLPARSER_SELECTOR_KIND_MERGE_INSERT_CELL:
			if (source->selector.row_index != source->dml_index ||
			    source->selector.item_index != source->row_index ||
			    source->selector.column_index != source->column_ordinal) {
				sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "query graph MERGE INSERT cell selector is invalid");
				return -1;
			}
			break;
		default:
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "query graph DML cell selector kind is invalid");
			return -1;
		}
		cell->selector_kind = (uint8_t)source->selector.kind;
		cell->flags |= SQLPARSER_GRAPH_DML_CELL_HAS_SELECTOR;
	}
	text_length = cache->value_text_length;
	switch (source->kind) {
	case SQLPARSER_GRAPH_VALUE_LITERAL:
		if (expression_sql != NULL || source->has_bind || source->has_bind_sql ||
		    source->has_bind_position || source->has_source_target ||
		    source->has_source_field) {
			break;
		}
		cell->payload.literal = source->literal;
		return 0;
	case SQLPARSER_GRAPH_VALUE_BIND:
		if (expression_sql != NULL || source->has_source_target ||
		    source->has_source_field) {
			break;
		}
		cell->payload.bind.bind_kind = source->bind_kind;
		cell->payload.bind.bind_position = source->bind_position;
		if (source->has_bind) {
			cell->flags |= SQLPARSER_GRAPH_DML_CELL_HAS_BIND;
		}
		if (source->has_bind_sql) {
			cell->flags |= SQLPARSER_GRAPH_DML_CELL_HAS_BIND_SQL;
		}
		if (source->has_bind_position) {
			cell->flags |= SQLPARSER_GRAPH_DML_CELL_HAS_BIND_POSITION;
		}
		if ((source->has_bind &&
		     sqlparser_graph_value_store_text(
			     cache,
			     source->bind,
			     &cell->payload.bind.bind_offset,
			     out_error) != 0) ||
		    (source->has_bind_sql &&
		     sqlparser_graph_value_store_text(
			     cache,
			     source->bind_sql,
			     &cell->payload.bind.bind_sql_offset,
			     out_error) != 0)) {
			cache->value_text_length = text_length;
			return -1;
		}
		return 0;
	case SQLPARSER_GRAPH_VALUE_DEFAULT:
		if (expression_sql == NULL && !source->has_bind &&
		    !source->has_bind_sql && !source->has_bind_position &&
		    !source->has_source_target && !source->has_source_field) {
			return 0;
		}
		break;
	case SQLPARSER_GRAPH_VALUE_EXPRESSION:
		if (expression_sql == NULL || expression_sql[0] == '\0' ||
		    source->has_bind || source->has_bind_sql ||
		    source->has_bind_position || source->has_source_target ||
		    source->has_source_field) {
			break;
		}
		if (sqlparser_graph_value_store_text(
			    cache,
			    expression_sql,
			    &cell->payload.expression_sql_offset,
			    out_error) != 0) {
			cache->value_text_length = text_length;
			return -1;
		}
		cell->flags |= SQLPARSER_GRAPH_DML_CELL_HAS_EXPRESSION_SQL;
		return 0;
	case SQLPARSER_GRAPH_VALUE_FIELD:
		if (expression_sql != NULL || source->has_bind || source->has_bind_sql ||
		    source->has_bind_position ||
		    (!source->has_source_target && !source->has_source_field)) {
			break;
		}
		cell->payload.field.source_target_index = source->source_target_index;
		cell->payload.field.source_field_index = source->source_field_index;
		if (source->has_source_target) {
			cell->flags |= SQLPARSER_GRAPH_DML_CELL_HAS_SOURCE_TARGET;
		}
		if (source->has_source_field) {
			cell->flags |= SQLPARSER_GRAPH_DML_CELL_HAS_SOURCE_FIELD;
		}
		return 0;
	default:
		break;
	}
	sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "query graph DML cell payload is invalid");
	return -1;
}

static int sqlparser_graph_dml_cell_cache_copy_public(
	const sqlparser_query_graph_cache_t *cache,
	const sqlparser_graph_dml_cell_cache_t *source,
	size_t statement_index,
	size_t cell_index,
	sqlparser_graph_dml_cell_t *cell,
	sqlparser_error_t *out_error)
{
	const char *text;

	if (cache == NULL || source == NULL || cell == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "invalid query graph DML cell cache");
		return -1;
	}
	memset(cell, 0, sizeof(*cell));
	cell->index = cell_index;
	cell->statement_index = statement_index;
	cell->dml_index = source->dml_index;
	cell->row_index = source->row_index;
	cell->column_ordinal = source->column_ordinal;
	cell->kind = (sqlparser_graph_value_kind_t)source->kind;
	cell->has_selector =
		(source->flags & SQLPARSER_GRAPH_DML_CELL_HAS_SELECTOR) != 0U;
	if (cell->has_selector) {
		cell->selector.kind =
			(sqlparser_selector_kind_t)source->selector_kind;
		cell->selector.statement_index = statement_index;
		switch (cell->selector.kind) {
		case SQLPARSER_SELECTOR_KIND_VALUE:
			cell->selector.item_index = source->selector_item_index;
			break;
		case SQLPARSER_SELECTOR_KIND_INSERT_CELL:
			cell->selector.row_index = source->row_index;
			cell->selector.column_index = source->column_ordinal;
			break;
		case SQLPARSER_SELECTOR_KIND_MERGE_INSERT_CELL:
			cell->selector.row_index = source->dml_index;
			cell->selector.item_index = source->row_index;
			cell->selector.column_index = source->column_ordinal;
			break;
		default:
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "query graph DML cell selector cache is invalid");
			return -1;
		}
	}
	switch (cell->kind) {
	case SQLPARSER_GRAPH_VALUE_LITERAL:
		cell->literal = source->payload.literal;
		return 0;
	case SQLPARSER_GRAPH_VALUE_BIND:
		cell->has_bind =
			(source->flags & SQLPARSER_GRAPH_DML_CELL_HAS_BIND) != 0U;
		cell->bind_kind = source->payload.bind.bind_kind;
		cell->has_bind_sql =
			(source->flags & SQLPARSER_GRAPH_DML_CELL_HAS_BIND_SQL) != 0U;
		cell->bind_position = source->payload.bind.bind_position;
		cell->has_bind_position =
			(source->flags & SQLPARSER_GRAPH_DML_CELL_HAS_BIND_POSITION) != 0U;
		if (cell->has_bind) {
			text = sqlparser_graph_value_text_at(
				cache,
				source->payload.bind.bind_offset,
				out_error);
			if (text == NULL) {
				return -1;
			}
			sqlparser_view_copy_public_text(
				cell->bind,
				sizeof(cell->bind),
				text,
				NULL);
		}
		if (cell->has_bind_sql) {
			text = sqlparser_graph_value_text_at(
				cache,
				source->payload.bind.bind_sql_offset,
				out_error);
			if (text == NULL) {
				return -1;
			}
			sqlparser_view_copy_public_text(
				cell->bind_sql,
				sizeof(cell->bind_sql),
				text,
				NULL);
		}
		return 0;
	case SQLPARSER_GRAPH_VALUE_DEFAULT:
	case SQLPARSER_GRAPH_VALUE_EXPRESSION:
		return 0;
	case SQLPARSER_GRAPH_VALUE_FIELD:
		cell->source_target_index =
			source->payload.field.source_target_index;
		cell->has_source_target =
			(source->flags & SQLPARSER_GRAPH_DML_CELL_HAS_SOURCE_TARGET) != 0U;
		cell->source_field_index =
			source->payload.field.source_field_index;
		cell->has_source_field =
			(source->flags & SQLPARSER_GRAPH_DML_CELL_HAS_SOURCE_FIELD) != 0U;
		return 0;
	default:
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "query graph DML cell kind is invalid");
		return -1;
	}
}

static int sqlparser_graph_target_cache_init(
	sqlparser_graph_target_cache_t *target,
	const sqlparser_graph_target_t *source,
	size_t statement_index,
	sqlparser_error_t *out_error)
{
	if (target == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "invalid query graph target cache");
		return -1;
	}
	memset(target, 0, sizeof(*target));
	target->block_index = source->block_index;
	target->ordinal = source->ordinal;
	target->kind = source->kind;
	target->output_name = source->output_name;
	target->field_index = source->field_index;
	target->value_index = source->value_index;
	target->star_relations = source->star_relations;
	target->source_block_index = source->source_block_index;
	target->sink_value_index = source->sink_value_index;
	if (source->has_field) {
		target->flags |= SQLPARSER_GRAPH_TARGET_HAS_FIELD;
	}
	if (source->has_value) {
		target->flags |= SQLPARSER_GRAPH_TARGET_HAS_VALUE;
	}
	if (source->has_source_block) {
		target->flags |= SQLPARSER_GRAPH_TARGET_HAS_SOURCE_BLOCK;
	}
	if (source->has_sink_value) {
		target->flags |= SQLPARSER_GRAPH_TARGET_HAS_SINK_VALUE;
	}
	if (source->has_selector) {
		if (source->selector.statement_index != statement_index ||
		    source->selector.column_index != source->ordinal) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "query graph target selector is invalid");
			return -1;
		}
		target->selector_kind = source->selector.kind;
		target->selector_item_index = source->selector.item_index;
		target->selector_row_index = source->selector.row_index;
		target->flags |= SQLPARSER_GRAPH_TARGET_HAS_SELECTOR;
	}
	if (source->has_target_list_selector) {
		if (source->target_list_selector.statement_index != statement_index ||
		    source->target_list_selector.column_index != 0U ||
		    (source->has_selector &&
		     (source->target_list_selector.item_index !=
			     source->selector.item_index ||
		      source->target_list_selector.row_index !=
			     source->selector.row_index))) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "query graph target-list selector is invalid");
			return -1;
		}
		target->target_list_selector_kind =
			source->target_list_selector.kind;
		if (!source->has_selector) {
			target->selector_item_index =
				source->target_list_selector.item_index;
			target->selector_row_index =
				source->target_list_selector.row_index;
		}
		target->flags |=
			SQLPARSER_GRAPH_TARGET_HAS_TARGET_LIST_SELECTOR;
	}
	return 0;
}

static void sqlparser_graph_target_cache_copy_public(
	const sqlparser_graph_target_cache_t *source,
	size_t statement_index,
	size_t target_index,
	sqlparser_graph_target_t *target)
{
	memset(target, 0, sizeof(*target));
	target->index = target_index;
	target->statement_index = statement_index;
	target->block_index = source->block_index;
	target->ordinal = source->ordinal;
	target->kind = source->kind;
	target->output_name = source->output_name;
	target->field_index = source->field_index;
	target->has_field =
		(source->flags & SQLPARSER_GRAPH_TARGET_HAS_FIELD) != 0U;
	target->value_index = source->value_index;
	target->has_value =
		(source->flags & SQLPARSER_GRAPH_TARGET_HAS_VALUE) != 0U;
	target->star_relations = source->star_relations;
	target->source_block_index = source->source_block_index;
	target->has_source_block =
		(source->flags & SQLPARSER_GRAPH_TARGET_HAS_SOURCE_BLOCK) != 0U;
	target->sink_value_index = source->sink_value_index;
	target->has_sink_value =
		(source->flags & SQLPARSER_GRAPH_TARGET_HAS_SINK_VALUE) != 0U;
	target->has_selector =
		(source->flags & SQLPARSER_GRAPH_TARGET_HAS_SELECTOR) != 0U;
	if (target->has_selector) {
		target->selector.kind = source->selector_kind;
		target->selector.statement_index = statement_index;
		target->selector.item_index = source->selector_item_index;
		target->selector.row_index = source->selector_row_index;
		target->selector.column_index = source->ordinal;
	}
	target->has_target_list_selector =
		(source->flags &
		 SQLPARSER_GRAPH_TARGET_HAS_TARGET_LIST_SELECTOR) != 0U;
	if (target->has_target_list_selector) {
		target->target_list_selector.kind =
			source->target_list_selector_kind;
		target->target_list_selector.statement_index = statement_index;
		target->target_list_selector.item_index =
			source->selector_item_index;
		target->target_list_selector.row_index =
			source->selector_row_index;
	}
}

static int sqlparser_graph_session_store_text(
	sqlparser_query_graph_cache_t *cache,
	const char *text,
	size_t length,
	size_t *out_offset,
	sqlparser_error_t *out_error)
{
	size_t required;

	if (cache == NULL || text == NULL || out_offset == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "invalid session graph text");
		return -1;
	}
	if (length > (size_t)-1 - cache->session_text_length - 1U) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_RESOURCE_LIMIT, "session graph text is too large");
		return -1;
	}
	required = cache->session_text_length + length + 1U;
	if (sqlparser_query_graph_reserve_array_with_initial(
		    (void **)&cache->session_text,
		    &cache->session_text_capacity,
		    required,
		    sizeof(*cache->session_text),
		    128U,
		    out_error) != 0) {
		return -1;
	}
	*out_offset = cache->session_text_length;
	if (length > 0U) {
		memcpy(cache->session_text + cache->session_text_length, text, length);
	}
	cache->session_text_length += length;
	cache->session_text[cache->session_text_length++] = '\0';
	return 0;
}

static int sqlparser_graph_session_set_action(
	sqlparser_graph_build_t *build,
	sqlparser_graph_session_action_t action,
	sqlparser_error_t *out_error)
{
	if (build == NULL || build->statement == NULL ||
	    action == SQLPARSER_GRAPH_SESSION_ACTION_UNKNOWN) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "invalid session graph action");
		return -1;
	}
	if (build->statement->session_action != SQLPARSER_GRAPH_SESSION_ACTION_UNKNOWN &&
	    build->statement->session_action != action) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "conflicting session graph action");
		return -1;
	}
	build->statement->session_action = action;
	return 0;
}

static int sqlparser_graph_session_add_item(
	sqlparser_graph_build_t *build,
	sqlparser_graph_session_scope_t scope,
	sqlparser_graph_session_target_kind_t target_kind,
	const char *name,
	size_t name_length,
	size_t *out_item_index,
	sqlparser_error_t *out_error)
{
	sqlparser_graph_session_item_cache_t *item;
	size_t global_index;

	if (build == NULL || build->cache == NULL || build->statement == NULL ||
	    target_kind == SQLPARSER_GRAPH_SESSION_TARGET_UNKNOWN) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "invalid session graph item");
		return -1;
	}
	if (sqlparser_query_graph_reserve_sparse_array(
		    (void **)&build->cache->session_items,
		    &build->cache->session_item_capacity,
		    build->cache->session_item_count + 1U,
		    sizeof(*build->cache->session_items),
		    out_error) != 0) {
		return -1;
	}
	global_index = build->cache->session_item_count++;
	item = &build->cache->session_items[global_index];
	memset(item, 0, sizeof(*item));
	item->scope = scope;
	item->target_kind = target_kind;
	item->value_offset = build->cache->session_value_count;
	if (name != NULL &&
	    sqlparser_graph_session_store_text(
		    build->cache,
		    name,
		    name_length,
		    &item->name_offset,
		    out_error) != 0) {
		build->cache->session_item_count--;
		return -1;
	}
	item->has_name = name != NULL;
	if (out_item_index != NULL) {
		*out_item_index = global_index - build->statement->session_item_offset;
	}
	return 0;
}

static int sqlparser_graph_session_resolve_bind_position(
	sqlparser_graph_build_t *build,
	const sqlparser_dialect_session_value_t *input,
	size_t *out_position,
	sqlparser_error_t *out_error)
{
	const char *sql;
	size_t end;
	size_t index;
	size_t left;
	size_t middle;
	size_t right;
	size_t start;
	size_t source_length;
	size_t target_offset;

	if (build == NULL || build->cache == NULL || build->handle == NULL ||
	    input == NULL || input->source_sql == NULL ||
	    out_position == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "session bind source is missing");
		return -1;
	}
	if (sqlparser_ensure_current_sql_text(build->handle, out_error) !=
		    SQLPARSER_STATUS_OK) {
		return -1;
	}
	sql = sqlparser_effective_sql(build->handle);
	if (sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "session bind source could not be resolved");
		return -1;
	}

	if (build->session_bind_source == NULL) {
		source_length = strlen(input->source_sql);
		if (input->source_offset >= source_length ||
		    !sqlparser_view_public_statement_span(
			    build->handle,
			    build->statement_index,
			    &start,
			    &end) ||
		    source_length > end - start) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "session bind source could not be resolved");
			return -1;
		}
		for (index = start; index <= end - source_length; index++) {
			if (memcmp(sql + index, input->source_sql, source_length) == 0) {
				build->session_bind_source = input->source_sql;
				build->session_bind_source_length = source_length;
				build->session_bind_source_start = index;
				break;
			}
		}
		if (build->session_bind_source == NULL) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "session bind source could not be resolved");
			return -1;
		}
	} else if (build->session_bind_source != input->source_sql ||
		   input->source_offset >= build->session_bind_source_length) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "session bind source changed while building query graph");
		return -1;
	}

	if (sqlparser_handle_ensure_bind_occurrences(
		    build->handle,
		    out_error) != SQLPARSER_STATUS_OK ||
	    build->handle->bind_occurrences == NULL) {
		return -1;
	}

	target_offset = build->session_bind_source_start + input->source_offset;
	left = 0U;
	right = build->handle->bind_occurrences->count;
	while (left < right) {
		middle = left + (right - left) / 2U;
		if (build->handle->bind_occurrences->items[middle].source_start <
		    target_offset) {
			left = middle + 1U;
		} else {
			right = middle;
		}
	}
	if (left < build->handle->bind_occurrences->count &&
	    build->handle->bind_occurrences->items[left].source_start ==
		    target_offset) {
		*out_position = left + 1U;
		return 0;
	}
	sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "session bind position could not be resolved");
	return -1;
}

static int sqlparser_graph_session_add_value(
	sqlparser_graph_build_t *build,
	size_t item_index,
	const sqlparser_dialect_session_value_t *input,
	sqlparser_error_t *out_error)
{
	char bind_key_buffer[32];
	const char *bind_key;
	sqlparser_graph_session_item_cache_t *item;
	sqlparser_graph_session_value_cache_t *value;
	size_t bind_key_length;
	size_t bind_position;
	size_t global_item_index;
	int has_bind_position;

	if (build == NULL || build->cache == NULL || build->statement == NULL || input == NULL ||
	    input->kind == SQLPARSER_GRAPH_SESSION_VALUE_UNKNOWN ||
	    build->cache->session_item_count < build->statement->session_item_offset ||
	    item_index >= build->cache->session_item_count - build->statement->session_item_offset) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "invalid session graph value");
		return -1;
	}
	global_item_index = build->statement->session_item_offset + item_index;
	item = &build->cache->session_items[global_item_index];
	if (item->value_offset + item->value_count != build->cache->session_value_count) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "session graph values must be contiguous");
		return -1;
	}
	if (sqlparser_query_graph_reserve_sparse_array(
		    (void **)&build->cache->session_values,
		    &build->cache->session_value_capacity,
		    build->cache->session_value_count + 1U,
		    sizeof(*build->cache->session_values),
		    out_error) != 0) {
		return -1;
	}
	value = &build->cache->session_values[build->cache->session_value_count];
	memset(value, 0, sizeof(*value));
	value->kind = input->kind;
	value->literal_kind = input->literal.kind;
	value->literal_integer = input->literal.integer_value;
	value->literal_boolean = input->literal.boolean_value;
	value->literal_quoted_identifier = input->literal.quoted_identifier;
	value->bind_kind = input->bind_kind;
	bind_position = input->bind_position;
	has_bind_position = input->has_bind_position;
	if (input->kind == SQLPARSER_GRAPH_SESSION_VALUE_BIND &&
	    !has_bind_position && input->source_sql != NULL) {
		if (sqlparser_graph_session_resolve_bind_position(
			    build, input, &bind_position, out_error) != 0) {
			return -1;
		}
		has_bind_position = 1;
	}
	value->bind_position = bind_position;
	value->has_bind_position = has_bind_position;
	bind_key = input->bind_key;
	bind_key_length = input->bind_key_length;
	if (input->kind == SQLPARSER_GRAPH_SESSION_VALUE_BIND &&
	    input->bind_kind == SQLPARSER_BIND_KIND_POSITIONAL &&
	    bind_key == NULL && has_bind_position) {
		(void)snprintf(
			bind_key_buffer,
			sizeof(bind_key_buffer),
			"%lu",
			(unsigned long)bind_position);
		bind_key = bind_key_buffer;
		bind_key_length = strlen(bind_key_buffer);
	}
	if (input->name != NULL &&
	    sqlparser_graph_session_store_text(
		    build->cache,
		    input->name,
		    input->name_length,
		    &value->name_offset,
		    out_error) != 0) {
		return -1;
	}
	value->has_name = input->name != NULL;
	if (input->text != NULL &&
	    sqlparser_graph_session_store_text(
		    build->cache,
		    input->text,
		    input->text_length,
		    &value->text_offset,
		    out_error) != 0) {
		return -1;
	}
	value->has_text = input->text != NULL;
	if (input->literal.kind == SQLPARSER_LITERAL_KIND_STRING &&
	    input->literal.string_value != NULL &&
	    sqlparser_graph_session_store_text(
		    build->cache,
		    input->literal.string_value,
		    strlen(input->literal.string_value),
		    &value->literal_text_offset,
		    out_error) != 0) {
		return -1;
	}
	if (input->literal.kind == SQLPARSER_LITERAL_KIND_FLOAT &&
	    input->literal.float_value != NULL &&
	    sqlparser_graph_session_store_text(
		    build->cache,
		    input->literal.float_value,
		    strlen(input->literal.float_value),
		    &value->literal_text_offset,
		    out_error) != 0) {
		return -1;
	}
	value->has_literal_text =
		(input->literal.kind == SQLPARSER_LITERAL_KIND_STRING && input->literal.string_value != NULL) ||
		(input->literal.kind == SQLPARSER_LITERAL_KIND_FLOAT && input->literal.float_value != NULL);
	if (bind_key != NULL &&
	    sqlparser_graph_session_store_text(
		    build->cache,
		    bind_key,
		    bind_key_length,
		    &value->bind_key_offset,
		    out_error) != 0) {
		return -1;
	}
	value->has_bind_key = bind_key != NULL;
	if (input->bind_sql != NULL &&
	    sqlparser_graph_session_store_text(
		    build->cache,
		    input->bind_sql,
		    input->bind_sql_length,
		    &value->bind_sql_offset,
		    out_error) != 0) {
		return -1;
	}
	value->has_bind_sql = input->bind_sql != NULL;
	build->cache->session_value_count++;
	item->value_count++;
	return 0;
}

static sqlparser_status_t sqlparser_graph_session_emit_action(
	void *context,
	sqlparser_graph_session_action_t action,
	sqlparser_error_t *out_error)
{
	return sqlparser_graph_session_set_action(
		       (sqlparser_graph_build_t *)context,
		       action,
		       out_error) == 0 ?
		SQLPARSER_STATUS_OK :
		(out_error != NULL ? out_error->code : SQLPARSER_STATUS_INTERNAL_ERROR);
}

static sqlparser_status_t sqlparser_graph_session_emit_item(
	void *context,
	sqlparser_graph_session_scope_t scope,
	sqlparser_graph_session_target_kind_t target_kind,
	const char *name,
	size_t name_length,
	size_t *out_item_index,
	sqlparser_error_t *out_error)
{
	return sqlparser_graph_session_add_item(
		       (sqlparser_graph_build_t *)context,
		       scope,
		       target_kind,
		       name,
		       name_length,
		       out_item_index,
		       out_error) == 0 ?
		SQLPARSER_STATUS_OK :
		(out_error != NULL ? out_error->code : SQLPARSER_STATUS_INTERNAL_ERROR);
}

static sqlparser_status_t sqlparser_graph_session_emit_value(
	void *context,
	size_t item_index,
	const sqlparser_dialect_session_value_t *value,
	sqlparser_error_t *out_error)
{
	return sqlparser_graph_session_add_value(
		       (sqlparser_graph_build_t *)context,
		       item_index,
		       value,
		       out_error) == 0 ?
		SQLPARSER_STATUS_OK :
		(out_error != NULL ? out_error->code : SQLPARSER_STATUS_INTERNAL_ERROR);
}

static int sqlparser_graph_session_add_ast_value(
	sqlparser_graph_build_t *build,
	size_t item_index,
	const char *name,
	PgQuery__Node *node,
	sqlparser_error_t *out_error);

static sqlparser_status_t sqlparser_graph_session_emit_ast_value(
	void *context,
	size_t item_index,
	const char *name,
	const PgQuery__Node *node,
	sqlparser_error_t *out_error)
{
	return sqlparser_graph_session_add_ast_value(
		       (sqlparser_graph_build_t *)context,
		       item_index,
		       name,
		       (PgQuery__Node *)node,
		       out_error) == 0 ?
		SQLPARSER_STATUS_OK :
		(out_error != NULL ? out_error->code : SQLPARSER_STATUS_INTERNAL_ERROR);
}

static int sqlparser_graph_session_parser_token_is_string(
	const sqlparser_handle_t *handle,
	int location)
{
	const char *sql;
	size_t length;
	size_t offset;

	if (handle == NULL || location < 0) {
		return 0;
	}
	sql = sqlparser_effective_parser_sql(handle);
	if (sql == NULL) {
		return 0;
	}
	length = strlen(sql);
	offset = (size_t)location;
	if (offset >= length) {
		return 0;
	}
	if (sql[offset] == '\'') {
		return 1;
	}
	if (offset + 1U < length &&
	    (sql[offset] == 'e' || sql[offset] == 'E') &&
	    sql[offset + 1U] == '\'') {
		return 1;
	}
	return offset + 2U < length &&
		(sql[offset] == 'u' || sql[offset] == 'U') &&
		sql[offset + 1U] == '&' &&
		sql[offset + 2U] == '\'';
}

static int sqlparser_graph_session_text_is_keyword(
	const char *text,
	sqlparser_identifier_source_t source)
{
	if (source.known && source.quoted) {
		return 0;
	}
	return sqlparser_text_equal_ci(text, "all") ||
		sqlparser_text_equal_ci(text, "current") ||
		sqlparser_text_equal_ci(text, "default") ||
		sqlparser_text_equal_ci(text, "false") ||
		sqlparser_text_equal_ci(text, "local") ||
		sqlparser_text_equal_ci(text, "none") ||
		sqlparser_text_equal_ci(text, "true");
}

static int sqlparser_graph_session_add_text_value(
	sqlparser_graph_build_t *build,
	size_t item_index,
	const char *name,
	const char *text,
	sqlparser_graph_session_value_kind_t kind,
	sqlparser_error_t *out_error)
{
	sqlparser_dialect_session_value_t value;

	if (text == NULL || text[0] == '\0') {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "session graph value text is missing");
		return -1;
	}
	memset(&value, 0, sizeof(value));
	value.name = name;
	value.name_length = name != NULL ? strlen(name) : 0U;
	value.kind = kind;
	value.text = text;
	value.text_length = strlen(text);
	return sqlparser_graph_session_add_value(build, item_index, &value, out_error);
}

static int sqlparser_graph_session_add_ast_value(
	sqlparser_graph_build_t *build,
	size_t item_index,
	const char *name,
	PgQuery__Node *node,
	sqlparser_error_t *out_error)
{
	sqlparser_dialect_session_value_t value;
	char *core_sql;
	char *public_sql;
	sqlparser_status_t status;
	const char *text;

	if (build == NULL || node == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "session AST value is missing");
		return -1;
	}
	node = sqlparser_unwrap_grouping_node(node);
	if (node == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "session AST value is missing");
		return -1;
	}
	memset(&value, 0, sizeof(value));
	value.name = name;
	value.name_length = name != NULL ? strlen(name) : 0U;
	if (node->node_case == PG_QUERY__NODE__NODE_A_CONST && node->a_const != NULL) {
		sqlparser_identifier_source_t source;

		status = sqlparser_fill_literal_view_from_a_const_with_sql(
			node->a_const,
			sqlparser_effective_parser_sql(build->handle),
			&value.literal,
			out_error);
		if (status == SQLPARSER_STATUS_OK &&
		    value.literal.kind == SQLPARSER_LITERAL_KIND_STRING &&
		    !sqlparser_graph_session_parser_token_is_string(
			    build->handle,
			    node->a_const->location)) {
			text = value.literal.string_value;
			if (!sqlparser_identifier_component_source(
				    build->handle,
				    node->a_const->location,
				    0U,
				    &source,
				    NULL)) {
				source.known = 1;
				source.quoted =
					value.literal.quoted_identifier;
				source.delimited = 0;
			}
			memset(&value.literal, 0, sizeof(value.literal));
			value.literal.quoted_identifier =
				source.known && source.delimited;
			value.kind = sqlparser_graph_session_text_is_keyword(
				text,
				source) ?
				SQLPARSER_GRAPH_SESSION_VALUE_KEYWORD :
					SQLPARSER_GRAPH_SESSION_VALUE_IDENTIFIER;
			value.text = text;
			value.text_length = text != NULL ? strlen(text) : 0U;
			return sqlparser_graph_session_add_value(build, item_index, &value, out_error);
		}
		if (status == SQLPARSER_STATUS_OK) {
			value.kind = SQLPARSER_GRAPH_SESSION_VALUE_LITERAL;
			return sqlparser_graph_session_add_value(build, item_index, &value, out_error);
		}
		if (status == SQLPARSER_STATUS_NO_MEMORY ||
		    status == SQLPARSER_STATUS_RESOURCE_LIMIT) {
			return -1;
		}
		sqlparser_error_clear(out_error);
	}
	if (node->node_case == PG_QUERY__NODE__NODE_STRING &&
	    sqlparser_node_string_value(node, &text)) {
		sqlparser_identifier_source_t source;

		source = sqlparser_identifier_source_for_text(
			build->handle,
			build->statement_index,
			text);
		return sqlparser_graph_session_add_text_value(
			build,
			item_index,
			name,
			text,
			sqlparser_graph_session_text_is_keyword(text, source) ?
				SQLPARSER_GRAPH_SESSION_VALUE_KEYWORD :
				SQLPARSER_GRAPH_SESSION_VALUE_IDENTIFIER,
			out_error);
	}
	if (node->node_case == PG_QUERY__NODE__NODE_ROLE_SPEC &&
	    node->role_spec != NULL) {
		switch (node->role_spec->roletype) {
			case PG_QUERY__ROLE_SPEC_TYPE__ROLESPEC_CSTRING:
				text = node->role_spec->rolename;
				break;
			case PG_QUERY__ROLE_SPEC_TYPE__ROLESPEC_CURRENT_ROLE:
				text = "CURRENT_ROLE";
				break;
			case PG_QUERY__ROLE_SPEC_TYPE__ROLESPEC_CURRENT_USER:
				text = "CURRENT_USER";
				break;
			case PG_QUERY__ROLE_SPEC_TYPE__ROLESPEC_SESSION_USER:
				text = "SESSION_USER";
				break;
			case PG_QUERY__ROLE_SPEC_TYPE__ROLESPEC_PUBLIC:
				text = "PUBLIC";
				break;
			default:
				text = NULL;
				break;
		}
		if (text != NULL) {
			return sqlparser_graph_session_add_text_value(
				build,
				item_index,
				name,
				text,
				node->role_spec->roletype ==
						PG_QUERY__ROLE_SPEC_TYPE__ROLESPEC_CSTRING ?
					SQLPARSER_GRAPH_SESSION_VALUE_IDENTIFIER :
					SQLPARSER_GRAPH_SESSION_VALUE_KEYWORD,
				out_error);
		}
	}
	if (node->node_case == PG_QUERY__NODE__NODE_SET_TO_DEFAULT) {
		return sqlparser_graph_session_add_text_value(
			build,
			item_index,
			name,
			"DEFAULT",
			SQLPARSER_GRAPH_SESSION_VALUE_KEYWORD,
			out_error);
	}
	if (node->node_case == PG_QUERY__NODE__NODE_DEF_ELEM &&
	    node->def_elem != NULL) {
		PgQuery__Node *arg;
		const char *value_name;

		arg = node->def_elem->arg;
		value_name = node->def_elem->defname;
		if (arg != NULL &&
		    arg->node_case == PG_QUERY__NODE__NODE_A_CONST &&
		    arg->a_const != NULL) {
			if (sqlparser_text_equal_ci(value_name, "transaction_isolation") &&
			    arg->a_const->val_case == PG_QUERY__A__CONST__VAL_SVAL &&
			    arg->a_const->sval != NULL) {
				return sqlparser_graph_session_add_text_value(
					build,
					item_index,
					"isolation_level",
					arg->a_const->sval->sval,
					SQLPARSER_GRAPH_SESSION_VALUE_KEYWORD,
					out_error);
			}
			if (sqlparser_text_equal_ci(value_name, "transaction_read_only") &&
			    arg->a_const->val_case == PG_QUERY__A__CONST__VAL_IVAL &&
			    arg->a_const->ival != NULL) {
				return sqlparser_graph_session_add_text_value(
					build,
					item_index,
					"access_mode",
					arg->a_const->ival->ival != 0 ? "READ ONLY" : "READ WRITE",
					SQLPARSER_GRAPH_SESSION_VALUE_KEYWORD,
					out_error);
			}
			if (sqlparser_text_equal_ci(value_name, "transaction_deferrable") &&
			    arg->a_const->val_case == PG_QUERY__A__CONST__VAL_IVAL &&
			    arg->a_const->ival != NULL) {
				return sqlparser_graph_session_add_text_value(
					build,
					item_index,
					"deferrable",
					arg->a_const->ival->ival != 0 ? "DEFERRABLE" : "NOT DEFERRABLE",
					SQLPARSER_GRAPH_SESSION_VALUE_KEYWORD,
					out_error);
			}
		}
		if (node->def_elem->arg == NULL) {
			return sqlparser_graph_session_add_text_value(
				build,
				item_index,
				node->def_elem->defname,
				node->def_elem->defname,
				SQLPARSER_GRAPH_SESSION_VALUE_KEYWORD,
				out_error);
		}
		return sqlparser_graph_session_add_ast_value(
			build,
			item_index,
			node->def_elem->defname,
			node->def_elem->arg,
			out_error);
	}
	if (node->node_case == PG_QUERY__NODE__NODE_PARAM_REF &&
	    node->param_ref != NULL) {
		sqlparser_view_bind_info_t bind_info;

		public_sql = NULL;
		status = sqlparser_view_render_param_ref_public_sql(
			build->handle,
			build->statement_index,
			node->param_ref,
			&public_sql,
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			free(public_sql);
			return -1;
		}
		status = sqlparser_view_bind_info_from_value(
			build->handle,
			build->bind_positions,
			public_sql,
			node,
			&bind_info,
			out_error);
		if (status <= 0) {
			free(public_sql);
			if (status == 0) {
				sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "session bind could not be resolved");
			}
			return -1;
		}
		value.kind = SQLPARSER_GRAPH_SESSION_VALUE_BIND;
		value.bind_key = bind_info.name;
		value.bind_key_length = strlen(bind_info.name);
		value.bind_kind = bind_info.kind;
		value.bind_sql = public_sql;
		value.bind_sql_length = strlen(public_sql);
		value.bind_position = bind_info.position;
		value.has_bind_position = bind_info.position != 0U;
		status = sqlparser_graph_session_add_value(
			build, item_index, &value, out_error);
		sqlparser_view_bind_info_release(&bind_info);
		free(public_sql);
		return status;
	}

	core_sql = NULL;
	public_sql = NULL;
	status = sqlparser_render_variable_set_arg_node_sql(
		build->handle,
		node,
		&core_sql,
		out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_postprocess_handle_sql_fragment(
			build->handle,
			build->statement_index,
			core_sql,
			SQLPARSER_FRAGMENT_CONTEXT_EXPRESSION,
			(ProtobufCMessage *const *)&node,
			1U,
			"session graph value",
			&public_sql,
			out_error);
	}
	free(core_sql);
	if (status != SQLPARSER_STATUS_OK) {
		free(public_sql);
		return -1;
	}
	status = sqlparser_graph_session_add_text_value(
		build,
		item_index,
		name,
		public_sql,
		SQLPARSER_GRAPH_SESSION_VALUE_EXPRESSION,
		out_error) == 0 ?
		SQLPARSER_STATUS_OK :
		(out_error != NULL ? out_error->code : SQLPARSER_STATUS_INTERNAL_ERROR);
	free(public_sql);
	return status == SQLPARSER_STATUS_OK ? 0 : -1;
}

static sqlparser_graph_session_target_kind_t sqlparser_graph_session_variable_target(
	const char *name)
{
	if (name == NULL) {
		return SQLPARSER_GRAPH_SESSION_TARGET_PARAMETER;
	}
	if (sqlparser_text_equal_ci(name, "search_path")) {
		return SQLPARSER_GRAPH_SESSION_TARGET_SCHEMA;
	}
	if (sqlparser_text_equal_ci(name, "role")) {
		return SQLPARSER_GRAPH_SESSION_TARGET_ROLE;
	}
	if (sqlparser_text_equal_ci(name, "session_authorization")) {
		return SQLPARSER_GRAPH_SESSION_TARGET_AUTHORIZATION;
	}
	if (sqlparser_text_equal_ci(name, "transaction") ||
	    sqlparser_text_equal_ci(name, "transaction snapshot") ||
	    sqlparser_text_equal_ci(name, "session characteristics")) {
		return SQLPARSER_GRAPH_SESSION_TARGET_TRANSACTION;
	}
	return SQLPARSER_GRAPH_SESSION_TARGET_PARAMETER;
}

static int sqlparser_graph_session_build_variable_set(
	sqlparser_graph_build_t *build,
	PgQuery__VariableSetStmt *statement,
	sqlparser_error_t *out_error)
{
	sqlparser_graph_session_target_kind_t target_kind;
	sqlparser_graph_session_scope_t scope;
	sqlparser_graph_session_action_t action;
	const char *name;
	size_t item_index;
	size_t index;

	if (statement == NULL) {
		return 0;
	}
	action = statement->kind == PG_QUERY__VARIABLE_SET_KIND__VAR_RESET ||
			statement->kind == PG_QUERY__VARIABLE_SET_KIND__VAR_RESET_ALL ?
		SQLPARSER_GRAPH_SESSION_ACTION_RESET :
		SQLPARSER_GRAPH_SESSION_ACTION_SET;
	if (sqlparser_graph_session_set_action(build, action, out_error) != 0) {
		return -1;
	}
	scope = statement->is_local ?
		SQLPARSER_GRAPH_SESSION_SCOPE_LOCAL :
		SQLPARSER_GRAPH_SESSION_SCOPE_SESSION;
	if (statement->kind == PG_QUERY__VARIABLE_SET_KIND__VAR_RESET_ALL) {
		return sqlparser_graph_session_add_item(
			build,
			scope,
			SQLPARSER_GRAPH_SESSION_TARGET_ALL,
			NULL,
			0U,
			NULL,
			out_error);
	}
	name = statement->name;
	target_kind = sqlparser_graph_session_variable_target(name);
	if (sqlparser_text_equal_ci(name, "transaction") ||
	    sqlparser_text_equal_ci(name, "transaction snapshot")) {
		scope = SQLPARSER_GRAPH_SESSION_SCOPE_TRANSACTION;
	}
	if (sqlparser_graph_session_add_item(
		    build,
		    scope,
		    target_kind,
		    target_kind == SQLPARSER_GRAPH_SESSION_TARGET_PARAMETER ||
				    target_kind == SQLPARSER_GRAPH_SESSION_TARGET_SCHEMA ?
			    name :
			    NULL,
		    target_kind == SQLPARSER_GRAPH_SESSION_TARGET_PARAMETER ||
				    target_kind == SQLPARSER_GRAPH_SESSION_TARGET_SCHEMA ?
			    strlen(name != NULL ? name : "") :
			    0U,
		    &item_index,
		    out_error) != 0) {
		return -1;
	}
	for (index = 0U; index < statement->n_args; index++) {
		if (sqlparser_graph_session_add_ast_value(
			    build,
			    item_index,
			    NULL,
			    statement->args[index],
			    out_error) != 0) {
			return -1;
		}
	}
	if (statement->n_args == 0U &&
	    statement->kind == PG_QUERY__VARIABLE_SET_KIND__VAR_SET_DEFAULT) {
		return sqlparser_graph_session_add_text_value(
			build,
			item_index,
			NULL,
			"DEFAULT",
			SQLPARSER_GRAPH_SESSION_VALUE_KEYWORD,
			out_error);
	}
	if (statement->n_args == 0U &&
	    statement->kind == PG_QUERY__VARIABLE_SET_KIND__VAR_SET_CURRENT) {
		return sqlparser_graph_session_add_text_value(
			build,
			item_index,
			NULL,
			"CURRENT",
			SQLPARSER_GRAPH_SESSION_VALUE_KEYWORD,
			out_error);
	}
	return 0;
}

static int sqlparser_graph_session_build_discard(
	sqlparser_graph_build_t *build,
	PgQuery__DiscardStmt *statement,
	sqlparser_error_t *out_error)
{
	const char *name;
	sqlparser_graph_session_target_kind_t target_kind;

	if (statement == NULL ||
	    sqlparser_graph_session_set_action(
		    build,
		    SQLPARSER_GRAPH_SESSION_ACTION_DISCARD,
		    out_error) != 0) {
		return statement == NULL ? 0 : -1;
	}
	target_kind = SQLPARSER_GRAPH_SESSION_TARGET_OBJECT;
	switch (statement->target) {
		case PG_QUERY__DISCARD_MODE__DISCARD_ALL:
			target_kind = SQLPARSER_GRAPH_SESSION_TARGET_ALL;
			name = NULL;
			break;
		case PG_QUERY__DISCARD_MODE__DISCARD_PLANS:
			name = "plans";
			break;
		case PG_QUERY__DISCARD_MODE__DISCARD_SEQUENCES:
			name = "sequences";
			break;
		case PG_QUERY__DISCARD_MODE__DISCARD_TEMP:
			name = "temporary";
			break;
		default:
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "discard target is invalid");
			return -1;
	}
	return sqlparser_graph_session_add_item(
		build,
		SQLPARSER_GRAPH_SESSION_SCOPE_SESSION,
		target_kind,
		name,
		name != NULL ? strlen(name) : 0U,
		NULL,
		out_error);
}

static int sqlparser_graph_session_constraint_name(
	PgQuery__Node *node,
	const char **out_schema,
	const char **out_name)
{
	if (out_schema == NULL || out_name == NULL) {
		return 0;
	}
	*out_schema = NULL;
	*out_name = NULL;
	if (sqlparser_node_string_value(node, out_name)) {
		return 1;
	}
	if (node != NULL &&
	    node->node_case == PG_QUERY__NODE__NODE_RANGE_VAR &&
	    node->range_var != NULL &&
	    node->range_var->relname != NULL) {
		*out_schema = node->range_var->schemaname;
		*out_name = node->range_var->relname;
		return 1;
	}
	return 0;
}

static int sqlparser_graph_session_build_constraints(
	sqlparser_graph_build_t *build,
	PgQuery__ConstraintsSetStmt *statement,
	sqlparser_error_t *out_error)
{
	const char *value_text;
	size_t index;

	if (statement == NULL) {
		return 0;
	}
	if (sqlparser_graph_session_set_action(
		    build,
		    SQLPARSER_GRAPH_SESSION_ACTION_SET,
		    out_error) != 0) {
		return -1;
	}
	value_text = statement->deferred ? "DEFERRED" : "IMMEDIATE";
	if (statement->n_constraints == 0U) {
		size_t item_index;

		if (sqlparser_graph_session_add_item(
			    build,
			    SQLPARSER_GRAPH_SESSION_SCOPE_TRANSACTION,
			    SQLPARSER_GRAPH_SESSION_TARGET_ALL,
			    "constraints",
			    strlen("constraints"),
			    &item_index,
			    out_error) != 0) {
			return -1;
		}
		return sqlparser_graph_session_add_text_value(
			build,
			item_index,
			NULL,
			value_text,
			SQLPARSER_GRAPH_SESSION_VALUE_KEYWORD,
			out_error);
	}
	for (index = 0U; index < statement->n_constraints; index++) {
		const char *schema;
		const char *name;
		char *qualified;
		const char *item_name;
		size_t item_name_length;
		size_t item_index;

		if (!sqlparser_graph_session_constraint_name(
			    statement->constraints[index],
			    &schema,
			    &name)) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "constraint name is invalid");
			return -1;
		}
		qualified = NULL;
		item_name = name;
		item_name_length = strlen(name);
		if (schema != NULL && schema[0] != '\0') {
			size_t schema_length;

			schema_length = strlen(schema);
			if (schema_length > (size_t)-1 - item_name_length - 2U) {
				sqlparser_error_set_message(out_error, SQLPARSER_STATUS_RESOURCE_LIMIT, "constraint name is too large");
				return -1;
			}
			qualified = (char *)malloc(schema_length + item_name_length + 2U);
			if (qualified == NULL) {
				sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
				return -1;
			}
			memcpy(qualified, schema, schema_length);
			qualified[schema_length] = '.';
			memcpy(qualified + schema_length + 1U, name, item_name_length + 1U);
			item_name = qualified;
			item_name_length += schema_length + 1U;
		}
		if (sqlparser_graph_session_add_item(
			    build,
			    SQLPARSER_GRAPH_SESSION_SCOPE_TRANSACTION,
			    SQLPARSER_GRAPH_SESSION_TARGET_CONSTRAINT,
			    item_name,
			    item_name_length,
			    &item_index,
			    out_error) != 0 ||
		    sqlparser_graph_session_add_text_value(
			    build,
			    item_index,
			    NULL,
			    value_text,
			    SQLPARSER_GRAPH_SESSION_VALUE_KEYWORD,
			    out_error) != 0) {
			free(qualified);
			return -1;
		}
		free(qualified);
	}
	return 0;
}

static int sqlparser_graph_build_native_session_statement(
	sqlparser_graph_build_t *build,
	PgQuery__Node *statement,
	sqlparser_error_t *out_error)
{
	if (build == NULL || statement == NULL || build->handle == NULL) {
		return 0;
	}
	switch (statement->node_case) {
		case PG_QUERY__NODE__NODE_VARIABLE_SET_STMT:
			return sqlparser_graph_session_build_variable_set(
				build,
				statement->variable_set_stmt,
				out_error);
		case PG_QUERY__NODE__NODE_DISCARD_STMT:
			return sqlparser_graph_session_build_discard(
				build,
				statement->discard_stmt,
				out_error);
		case PG_QUERY__NODE__NODE_CONSTRAINTS_SET_STMT:
			return sqlparser_graph_session_build_constraints(
				build,
				statement->constraints_set_stmt,
				out_error);
		default:
			return 0;
	}
}

static int sqlparser_graph_build_session_statement(
	sqlparser_graph_build_t *build,
	PgQuery__Node *statement,
	sqlparser_error_t *out_error)
{
	sqlparser_dialect_session_emitter_t emitter;
	sqlparser_status_t status;
	int is_internal_rewrite;

	if (build == NULL || build->handle == NULL || statement == NULL) {
		return 0;
	}
	is_internal_rewrite =
		statement->node_case == PG_QUERY__NODE__NODE_VARIABLE_SET_STMT &&
		statement->variable_set_stmt != NULL &&
		sqlparser_variable_set_name_is_internal(
			statement->variable_set_stmt) &&
		sqlparser_view_variable_set_is_internal_rewrite(
			build->handle,
			build->statement_index,
			statement->variable_set_stmt);
	memset(&emitter, 0, sizeof(emitter));
	emitter.context = build;
	emitter.set_action = sqlparser_graph_session_emit_action;
	emitter.add_item = sqlparser_graph_session_emit_item;
	emitter.add_value = sqlparser_graph_session_emit_value;
	emitter.add_ast_value = sqlparser_graph_session_emit_ast_value;
	if ((!sqlparser_variable_set_name_is_internal(
		     statement->node_case == PG_QUERY__NODE__NODE_VARIABLE_SET_STMT ?
			     statement->variable_set_stmt :
			     NULL) ||
	     is_internal_rewrite) &&
	    build->handle->dialect_ops != NULL &&
	    build->handle->dialect_ops->project_session != NULL) {
		status = build->handle->dialect_ops->project_session(
			build->handle,
			build->handle->dialect_state,
			build->statement_index,
			statement,
			&emitter,
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			return -1;
		}
	}
	if (build->statement->session_action != SQLPARSER_GRAPH_SESSION_ACTION_UNKNOWN) {
		return 0;
	}
	if (is_internal_rewrite) {
		return 0;
	}
	return sqlparser_graph_build_native_session_statement(build, statement, out_error);
}

static int sqlparser_graph_span_append_index(
	sqlparser_graph_build_t *build,
	sqlparser_index_span_t *span,
	size_t value,
	sqlparser_error_t *out_error);

static sqlparser_graph_dml_result_cache_t *sqlparser_graph_dml_result_cache_ensure(
	sqlparser_graph_build_t *build,
	sqlparser_error_t *out_error)
{
	if (build == NULL || build->cache == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "query graph builder is missing");
		return NULL;
	}
	if (build->cache->dml_results == NULL) {
		build->cache->dml_results = (sqlparser_graph_dml_result_cache_t *)calloc(
			1U,
			sizeof(*build->cache->dml_results));
		if (build->cache->dml_results == NULL) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return NULL;
		}
	}
	return build->cache->dml_results;
}

static int sqlparser_graph_dml_result_add_meta(
	sqlparser_graph_build_t *build,
	size_t dml_index,
	size_t parent_dml_index,
	int has_parent,
	size_t *out_meta_index,
	sqlparser_error_t *out_error)
{
	sqlparser_graph_dml_result_cache_t *result_cache;
	sqlparser_graph_dml_result_meta_t *meta;
	size_t global_dml_index;
	size_t index;
	size_t required;

	if (out_meta_index == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "DML result metadata output is missing");
		return -1;
	}
	*out_meta_index = 0U;
	result_cache = sqlparser_graph_dml_result_cache_ensure(build, out_error);
	if (result_cache == NULL) {
		return -1;
	}
	global_dml_index = build->statement->dml_offset + dml_index;
	if (global_dml_index == SIZE_MAX) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_RESOURCE_LIMIT, "DML result metadata is too large");
		return -1;
	}
	required = global_dml_index + 1U;
	if (sqlparser_query_graph_reserve_sparse_array(
		    (void **)&result_cache->metas,
		    &result_cache->meta_capacity,
		    required,
		    sizeof(*result_cache->metas),
		    out_error) != 0) {
		return -1;
	}
	for (index = result_cache->meta_count; index < required; index++) {
		memset(&result_cache->metas[index], 0, sizeof(result_cache->metas[index]));
		result_cache->metas[index].dml_global_index = SIZE_MAX;
	}
	if (result_cache->meta_count < required) {
		result_cache->meta_count = required;
	}
	*out_meta_index = global_dml_index;
	meta = &result_cache->metas[global_dml_index];
	if (meta->dml_global_index != SIZE_MAX) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "DML result metadata already exists");
		return -1;
	}
	memset(meta, 0, sizeof(*meta));
	meta->dml_global_index = global_dml_index;
	meta->result_offset = result_cache->result_count;
	meta->parent_dml_index = parent_dml_index;
	meta->has_parent = has_parent;
	return 0;
}

static int sqlparser_graph_dml_result_add_result(
	sqlparser_graph_build_t *build,
	size_t meta_index,
	const sqlparser_graph_dml_result_t *source,
	size_t *out_result_index,
	sqlparser_error_t *out_error)
{
	sqlparser_graph_dml_result_cache_t *result_cache;
	sqlparser_graph_dml_result_meta_t *meta;
	sqlparser_graph_dml_result_t *result;

	if (out_result_index == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "DML result output is missing");
		return -1;
	}
	*out_result_index = 0U;
	result_cache = sqlparser_graph_dml_result_cache_ensure(build, out_error);
	if (result_cache == NULL) {
		return -1;
	}
	if (meta_index >= result_cache->meta_count ||
	    result_cache->metas[meta_index].dml_global_index != meta_index) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "DML result metadata is missing");
		return -1;
	}
	if (sqlparser_query_graph_reserve_sparse_array(
		    (void **)&result_cache->results,
		    &result_cache->result_capacity,
		    result_cache->result_count + 1U,
		    sizeof(*result_cache->results),
		    out_error) != 0) {
		return -1;
	}
	*out_result_index = result_cache->result_count;
	result = &result_cache->results[result_cache->result_count++];
	memset(result, 0, sizeof(*result));
	if (source != NULL) {
		*result = *source;
	}
	meta = &result_cache->metas[meta_index];
	meta->result_count++;
	return 0;
}

static int sqlparser_graph_dml_result_add_reference(
	sqlparser_graph_build_t *build,
	const sqlparser_graph_dml_reference_t *source,
	sqlparser_error_t *out_error)
{
	sqlparser_graph_dml_result_cache_t *result_cache;
	sqlparser_graph_dml_reference_t *reference;
	size_t local_index;

	if (build == NULL || !build->has_dml_result || source == NULL) {
		return 0;
	}
	result_cache = sqlparser_graph_dml_result_cache_ensure(build, out_error);
	if (result_cache == NULL) {
		return -1;
	}
	if (build->dml_result_index >= result_cache->result_count) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "active DML result index is invalid");
		return -1;
	}
	if (sqlparser_query_graph_reserve_sparse_array(
		    (void **)&result_cache->references,
		    &result_cache->reference_capacity,
		    result_cache->reference_count + 1U,
		    sizeof(*result_cache->references),
		    out_error) != 0) {
		return -1;
	}
	local_index = result_cache->reference_count - build->statement->dml_reference_offset;
	reference = &result_cache->references[result_cache->reference_count++];
	*reference = *source;
	return sqlparser_graph_span_append_index(
		build,
		&result_cache->results[build->dml_result_index].references,
		local_index,
		out_error);
}

static int sqlparser_graph_dml_result_add_resolved_references(
	sqlparser_graph_build_t *build,
	size_t target_index,
	size_t field_index,
	int has_field,
	size_t relation_index,
	unsigned int target_reference_kinds,
	int source_reference,
	sqlparser_error_t *out_error)
{
	sqlparser_graph_dml_reference_t reference;

	memset(&reference, 0, sizeof(reference));
	reference.target_index = target_index;
	reference.field_index = field_index;
	reference.has_field = has_field;
	reference.relation_index = relation_index;
	if (source_reference) {
		reference.kind = SQLPARSER_GRAPH_DML_REFERENCE_SOURCE;
		return sqlparser_graph_dml_result_add_reference(
			build, &reference, out_error);
	}
	if ((target_reference_kinds &
	     SQLPARSER_DIALECT_DML_TARGET_REFERENCE_BEFORE) != 0U) {
		reference.kind = SQLPARSER_GRAPH_DML_REFERENCE_TARGET_BEFORE;
		if (sqlparser_graph_dml_result_add_reference(
			    build, &reference, out_error) != 0) {
			return -1;
		}
	}
	if ((target_reference_kinds &
	     SQLPARSER_DIALECT_DML_TARGET_REFERENCE_AFTER) != 0U) {
		reference.kind = SQLPARSER_GRAPH_DML_REFERENCE_TARGET_AFTER;
		if (sqlparser_graph_dml_result_add_reference(
			    build, &reference, out_error) != 0) {
			return -1;
		}
	}
	return 0;
}

static int sqlparser_graph_dml_result_own_text(
	sqlparser_graph_build_t *build,
	char *text,
	sqlparser_error_t *out_error)
{
	sqlparser_graph_dml_result_cache_t *result_cache;

	result_cache = sqlparser_graph_dml_result_cache_ensure(build, out_error);
	if (result_cache == NULL ||
	    sqlparser_query_graph_reserve_sparse_array(
		    (void **)&result_cache->owned_texts,
		    &result_cache->owned_text_capacity,
		    result_cache->owned_text_count + 1U,
		    sizeof(*result_cache->owned_texts),
		    out_error) != 0) {
		free(text);
		return -1;
	}
	result_cache->owned_texts[result_cache->owned_text_count++] = text;
	return 0;
}

void sqlparser_query_graph_cache_release(sqlparser_query_graph_cache_t *cache)
{
	size_t text_index;

	if (cache == NULL) {
		return;
	}
	free(cache->statements);
	free(cache->blocks);
	free(cache->relations);
	free(cache->targets);
	free(cache->fields);
	free(cache->values);
	free(cache->value_text);
	free(cache->like_escapes);
	free(cache->sets);
	free(cache->predicates);
	free(cache->dml);
	free(cache->dml_branches);
	free(cache->merge_branch_details);
	free(cache->dml_columns);
	free(cache->dml_cells);
	free(cache->dml_assignments);
	free(cache->session_items);
	free(cache->session_values);
	free(cache->session_text);
	if (cache->dml_results != NULL) {
		for (text_index = 0U; text_index < cache->dml_results->owned_text_count; text_index++) {
			free(cache->dml_results->owned_texts[text_index]);
		}
		free(cache->dml_results->metas);
		free(cache->dml_results->results);
		free(cache->dml_results->references);
		free(cache->dml_results->owned_texts);
		free(cache->dml_results);
	}
	free(cache->index_pool);
	free(cache);
}

static int sqlparser_graph_append_index(
	sqlparser_graph_build_t *build,
	size_t value,
	sqlparser_error_t *out_error)
{
	sqlparser_query_graph_cache_t *cache;

	if (build == NULL || build->cache == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "query graph builder is missing");
		return -1;
	}
	cache = build->cache;
	if (cache->index_pool_count == SIZE_MAX) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_RESOURCE_LIMIT, "query graph is too large");
		return -1;
	}
	if (sqlparser_query_graph_reserve_array_with_initial(
		    (void **)&cache->index_pool,
		    &cache->index_pool_capacity,
		    cache->index_pool_count + 1U,
		    sizeof(*cache->index_pool),
		    4U,
		    out_error) != 0) {
		return -1;
	}
	cache->index_pool[cache->index_pool_count++] = value;
	return 0;
}

#define SQLPARSER_GRAPH_INDEX_SEGMENT_MARKER SIZE_MAX

enum {
	SQLPARSER_GRAPH_INDEX_SEGMENT_HEADER_COUNT = 3U,
	SQLPARSER_GRAPH_INDEX_SEGMENT_INITIAL_CAPACITY = 4U
};

static int sqlparser_graph_span_segment_info(
	const sqlparser_query_graph_cache_t *cache,
	const sqlparser_index_span_t *span,
	size_t *out_capacity,
	size_t *out_used)
{
	size_t capacity;
	size_t header_offset;
	size_t used;

	*out_capacity = 0U;
	*out_used = 0U;
	if (span->offset < SQLPARSER_GRAPH_INDEX_SEGMENT_HEADER_COUNT) {
		return 0;
	}
	header_offset =
		span->offset - SQLPARSER_GRAPH_INDEX_SEGMENT_HEADER_COUNT;
	if (cache->index_pool[header_offset] !=
	    SQLPARSER_GRAPH_INDEX_SEGMENT_MARKER) {
		return 0;
	}
	capacity = cache->index_pool[header_offset + 1U];
	used = cache->index_pool[header_offset + 2U];
	if (capacity < SQLPARSER_GRAPH_INDEX_SEGMENT_INITIAL_CAPACITY ||
	    (capacity & (capacity - 1U)) != 0U ||
	    used > capacity ||
	    capacity > cache->index_pool_count - span->offset) {
		return -1;
	}
	*out_capacity = capacity;
	*out_used = used;
	return 1;
}

static int sqlparser_graph_span_append_segment(
	sqlparser_graph_build_t *build,
	sqlparser_index_span_t *span,
	size_t value,
	sqlparser_error_t *out_error)
{
	sqlparser_query_graph_cache_t *cache;
	size_t header_offset;
	size_t new_capacity;
	size_t new_count;
	size_t required;
	size_t values_offset;

	cache = build->cache;
	new_capacity = SQLPARSER_GRAPH_INDEX_SEGMENT_INITIAL_CAPACITY;
	while (new_capacity <= span->count) {
		if (new_capacity > SIZE_MAX / 2U) {
			sqlparser_error_set_message(
				out_error,
				SQLPARSER_STATUS_RESOURCE_LIMIT,
				"query graph is too large");
			return -1;
		}
		new_capacity *= 2U;
	}
	if (new_capacity > SIZE_MAX / sizeof(*cache->index_pool) ||
	    cache->index_pool_count >
		    SIZE_MAX - SQLPARSER_GRAPH_INDEX_SEGMENT_HEADER_COUNT ||
	    new_capacity >
		    SIZE_MAX -
			    SQLPARSER_GRAPH_INDEX_SEGMENT_HEADER_COUNT -
			    cache->index_pool_count) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_RESOURCE_LIMIT,
			"query graph is too large");
		return -1;
	}
	header_offset = cache->index_pool_count;
	values_offset =
		header_offset + SQLPARSER_GRAPH_INDEX_SEGMENT_HEADER_COUNT;
	required = values_offset + new_capacity;
	if (sqlparser_query_graph_reserve_array_with_initial(
		    (void **)&cache->index_pool,
		    &cache->index_pool_capacity,
		    required,
		    sizeof(*cache->index_pool),
		    4U,
		    out_error) != 0) {
		return -1;
	}
	memset(
		cache->index_pool + values_offset,
		0,
		new_capacity * sizeof(*cache->index_pool));
	if (span->count > 0U) {
		memcpy(
			cache->index_pool + values_offset,
			cache->index_pool + span->offset,
			span->count * sizeof(*cache->index_pool));
	}
	new_count = span->count + 1U;
	cache->index_pool[values_offset + span->count] = value;
	cache->index_pool[header_offset] =
		SQLPARSER_GRAPH_INDEX_SEGMENT_MARKER;
	cache->index_pool[header_offset + 1U] = new_capacity;
	cache->index_pool[header_offset + 2U] = new_count;
	cache->index_pool_count = required;
	span->offset = values_offset;
	span->count = new_count;
	return 0;
}

static int sqlparser_graph_span_append_index(
	sqlparser_graph_build_t *build,
	sqlparser_index_span_t *span,
	size_t value,
	sqlparser_error_t *out_error)
{
	sqlparser_query_graph_cache_t *cache;
	size_t capacity;
	size_t offset;
	size_t used;
	int segment_status;

	if (span == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "query graph span is missing");
		return -1;
	}
	if (build == NULL || build->cache == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "query graph builder is missing");
		return -1;
	}
	cache = build->cache;
	if (value == SQLPARSER_GRAPH_INDEX_SEGMENT_MARKER) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "query graph index is invalid");
		return -1;
	}
	if (span->count == 0U) {
		offset = cache->index_pool_count;
		if (sqlparser_graph_append_index(build, value, out_error) != 0) {
			return -1;
		}
		span->offset = offset;
		span->count = 1U;
		return 0;
	}
	if (span->offset > cache->index_pool_count ||
	    span->count > cache->index_pool_count - span->offset) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "query graph span is invalid");
		return -1;
	}
	capacity = 0U;
	used = 0U;
	segment_status = sqlparser_graph_span_segment_info(
		cache,
		span,
		&capacity,
		&used);
	if (segment_status < 0 || (segment_status > 0 && used < span->count)) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INTERNAL_ERROR,
			"query graph index segment is invalid");
		return -1;
	}
	if (segment_status > 0) {
		if (used == span->count && used < capacity) {
			cache->index_pool[span->offset + used] = value;
			cache->index_pool[span->offset - 1U] = used + 1U;
			span->count++;
			return 0;
		}
		return sqlparser_graph_span_append_segment(
			build,
			span,
			value,
			out_error);
	}
	if (span->offset + span->count != cache->index_pool_count) {
		return sqlparser_graph_span_append_segment(
			build,
			span,
			value,
			out_error);
	}
	if (sqlparser_graph_append_index(build, value, out_error) != 0) {
		return -1;
	}
	span->count++;
	return 0;
}

static int sqlparser_graph_span_append_unique_index(
	sqlparser_graph_build_t *build,
	sqlparser_index_span_t *span,
	size_t value,
	sqlparser_error_t *out_error)
{
	size_t index;

	if (build == NULL || build->cache == NULL || span == NULL ||
	    span->offset > build->cache->index_pool_count ||
	    span->count > build->cache->index_pool_count - span->offset) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INTERNAL_ERROR,
			"query graph span is invalid");
		return -1;
	}
	for (index = 0U; index < span->count; index++) {
		if (build->cache->index_pool[span->offset + index] == value) {
			return 0;
		}
	}
	return sqlparser_graph_span_append_index(
		build,
		span,
		value,
		out_error);
}

static int sqlparser_graph_rhs_capture_is_active(
	const sqlparser_graph_build_t *build,
	size_t block_index)
{
	return build != NULL && !build->collect_relation_bindings &&
		build->rhs_capture_assignment != NULL &&
		build->rhs_capture_block_index == block_index;
}

static size_t sqlparser_graph_local_block_count(const sqlparser_graph_build_t *build)
{
	return build->cache->block_count - build->statement->block_offset;
}

static size_t sqlparser_graph_local_relation_count(const sqlparser_graph_build_t *build)
{
	return build->cache->relation_count - build->statement->relation_offset;
}

static size_t sqlparser_graph_local_target_count(const sqlparser_graph_build_t *build)
{
	return build->cache->target_count - build->statement->target_offset;
}

static size_t sqlparser_graph_local_field_count(const sqlparser_graph_build_t *build)
{
	return build->cache->field_count - build->statement->field_offset;
}

static size_t sqlparser_graph_local_value_count(const sqlparser_graph_build_t *build)
{
	return build->cache->value_count - build->statement->value_offset;
}

static size_t sqlparser_graph_local_set_count(const sqlparser_graph_build_t *build)
{
	return build->cache->set_count - build->statement->set_offset;
}

static size_t sqlparser_graph_local_predicate_count(const sqlparser_graph_build_t *build)
{
	return build->cache->predicate_count - build->statement->predicate_offset;
}

static int sqlparser_graph_add_block(
	sqlparser_graph_build_t *build,
	sqlparser_graph_block_kind_t kind,
	size_t *out_index,
	sqlparser_error_t *out_error)
{
	sqlparser_graph_block_t *block;
	size_t global_index;
	size_t local_index;

	if (out_index != NULL) {
		*out_index = 0U;
	}
	if (build == NULL || build->cache == NULL || build->statement == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "query graph builder is missing");
		return -1;
	}
	if (sqlparser_query_graph_reserve_array_with_initial(
		    (void **)&build->cache->blocks,
		    &build->cache->block_capacity,
		    build->cache->block_count + 1U,
		    sizeof(*build->cache->blocks),
		    4U,
		    out_error) != 0) {
		return -1;
	}
	global_index = build->cache->block_count++;
	local_index = sqlparser_graph_local_block_count(build) - 1U;
	block = &build->cache->blocks[global_index];
	memset(block, 0, sizeof(*block));
	block->index = local_index;
	block->statement_index = build->statement_index;
	block->kind = kind;
	if (!build->statement->has_root_block) {
		build->statement->root_block_index = local_index;
		build->statement->has_root_block = 1;
	}
	if (out_index != NULL) {
		*out_index = local_index;
	}
	return 0;
}

static sqlparser_graph_block_t *sqlparser_graph_block_by_local(
	sqlparser_graph_build_t *build,
	size_t block_index)
{
	if (build == NULL || build->statement == NULL ||
	    block_index >= sqlparser_graph_local_block_count(build)) {
		return NULL;
	}
	return &build->cache->blocks[build->statement->block_offset + block_index];
}

static sqlparser_graph_relation_t *sqlparser_graph_relation_by_local(
	sqlparser_graph_build_t *build,
	size_t relation_index)
{
	if (build == NULL || build->statement == NULL ||
	    relation_index >= sqlparser_graph_local_relation_count(build)) {
		return NULL;
	}
	return &build->cache->relations[build->statement->relation_offset + relation_index];
}

static sqlparser_graph_relation_identifier_t *
sqlparser_graph_relation_identifier_by_local(
	sqlparser_graph_build_t *build,
	size_t relation_index)
{
	if (build == NULL ||
	    relation_index >= build->relation_identifier_count) {
		return NULL;
	}
	return &build->relation_identifiers[relation_index];
}

static sqlparser_graph_target_cache_t *sqlparser_graph_target_by_local(
	sqlparser_graph_build_t *build,
	size_t target_index)
{
	if (build == NULL || build->statement == NULL ||
	    target_index >= sqlparser_graph_local_target_count(build)) {
		return NULL;
	}
	return &build->cache->targets[build->statement->target_offset + target_index];
}

static sqlparser_identifier_source_t *
sqlparser_graph_target_identifier_by_local(
	sqlparser_graph_build_t *build,
	size_t target_index)
{
	if (build == NULL ||
	    target_index >= build->target_identifier_count) {
		return NULL;
	}
	return &build->target_identifiers[target_index];
}

static sqlparser_graph_field_t *sqlparser_graph_field_by_local(
	sqlparser_graph_build_t *build,
	size_t field_index)
{
	if (build == NULL || build->statement == NULL ||
	    field_index >= sqlparser_graph_local_field_count(build)) {
		return NULL;
	}
	return &build->cache->fields[build->statement->field_offset + field_index];
}

static int sqlparser_graph_add_relation(
	sqlparser_graph_build_t *build,
	size_t block_index,
	sqlparser_graph_relation_kind_t kind,
	const sqlparser_relation_view_t *relation_view,
	const sqlparser_graph_relation_identifier_t *identifiers,
	size_t *out_index,
	sqlparser_error_t *out_error)
{
	sqlparser_graph_relation_identifier_t *relation_identifiers;
	sqlparser_graph_relation_t *relation;
	size_t global_index;
	size_t local_index;

	if (out_index != NULL) {
		*out_index = 0U;
	}
	if (sqlparser_query_graph_reserve_sparse_array(
		    (void **)&build->relation_identifiers,
		    &build->relation_identifier_capacity,
		    build->relation_identifier_count + 1U,
		    sizeof(*build->relation_identifiers),
		    out_error) != 0) {
		return -1;
	}
	if (sqlparser_query_graph_reserve_array(
		    (void **)&build->cache->relations,
		    &build->cache->relation_capacity,
		    build->cache->relation_count + 1U,
		    sizeof(*build->cache->relations),
		    out_error) != 0) {
		return -1;
	}
	global_index = build->cache->relation_count++;
	local_index = sqlparser_graph_local_relation_count(build) - 1U;
	relation = &build->cache->relations[global_index];
	memset(relation, 0, sizeof(*relation));
	relation->index = local_index;
	relation->statement_index = build->statement_index;
	relation->block_index = block_index;
	relation->kind = kind;
	relation_identifiers =
		&build->relation_identifiers[build->relation_identifier_count++];
	if (identifiers != NULL) {
		*relation_identifiers = *identifiers;
	} else {
		memset(relation_identifiers, 0, sizeof(*relation_identifiers));
	}
	relation->quoted_identifier =
		relation_identifiers->object.known &&
		relation_identifiers->object.delimited;
	if (relation_view != NULL) {
		relation->database_name = relation_view->database_name;
		relation->schema_name = relation_view->schema_name;
		relation->object_name = relation_view->table_name;
		relation->alias_name = relation_view->alias_name;
		relation->link_name = relation_view->link_name;
	}
	if (out_index != NULL) {
		*out_index = local_index;
	}
	return 0;
}

static int sqlparser_graph_add_target(
	sqlparser_graph_build_t *build,
	const sqlparser_graph_target_t *source,
	const sqlparser_identifier_source_t *identifier,
	size_t *out_index,
	sqlparser_error_t *out_error)
{
	sqlparser_identifier_source_t *target_identifier;
	sqlparser_graph_target_cache_t target;
	size_t global_index;
	size_t local_index;

	if (out_index != NULL) {
		*out_index = 0U;
	}
	if (source == NULL || identifier == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "query graph target source or identifier is missing");
		return -1;
	}
	if (sqlparser_query_graph_reserve_sparse_array(
		    (void **)&build->target_identifiers,
		    &build->target_identifier_capacity,
		    build->target_identifier_count + 1U,
		    sizeof(*build->target_identifiers),
		    out_error) != 0 ||
	    sqlparser_query_graph_reserve_array_with_initial(
		    (void **)&build->cache->targets,
		    &build->cache->target_capacity,
		    build->cache->target_count + 1U,
		    sizeof(*build->cache->targets),
		    4U,
		    out_error) != 0) {
		return -1;
	}
	global_index = build->cache->target_count;
	local_index = sqlparser_graph_local_target_count(build);
	if (sqlparser_graph_target_cache_init(
		    &target,
		    source,
		    build->statement_index,
		    out_error) != 0) {
		return -1;
	}
	build->cache->targets[global_index] = target;
	build->cache->target_count++;
	target_identifier =
		&build->target_identifiers[build->target_identifier_count++];
	*target_identifier = *identifier;
	if (out_index != NULL) {
		*out_index = local_index;
	}
	return 0;
}

static int sqlparser_graph_add_field(
	sqlparser_graph_build_t *build,
	const sqlparser_graph_field_t *source,
	size_t *out_index,
	sqlparser_error_t *out_error)
{
	sqlparser_graph_field_t *field;
	size_t global_index;
	size_t local_index;

	if (out_index != NULL) {
		*out_index = 0U;
	}
	if (sqlparser_query_graph_reserve_array(
		    (void **)&build->cache->fields,
		    &build->cache->field_capacity,
		    build->cache->field_count + 1U,
		    sizeof(*build->cache->fields),
		    out_error) != 0) {
		return -1;
	}
	global_index = build->cache->field_count++;
	local_index = sqlparser_graph_local_field_count(build) - 1U;
	field = &build->cache->fields[global_index];
	memset(field, 0, sizeof(*field));
	if (source != NULL) {
		*field = *source;
	}
	field->index = local_index;
	field->statement_index = build->statement_index;
	if (out_index != NULL) {
		*out_index = local_index;
	}
	return 0;
}

static int sqlparser_graph_selector_equal(
	const sqlparser_selector_t *left,
	const sqlparser_selector_t *right)
{
	return left != NULL &&
		right != NULL &&
		left->kind == right->kind &&
		left->statement_index == right->statement_index &&
		left->item_index == right->item_index &&
		left->row_index == right->row_index &&
		left->column_index == right->column_index;
}

static int sqlparser_graph_add_value(
	sqlparser_graph_build_t *build,
	const sqlparser_graph_value_t *source,
	size_t *out_index,
	sqlparser_error_t *out_error)
{
	sqlparser_graph_like_escape_cache_t escape;
	sqlparser_graph_value_cache_t value;
	size_t global_index;
	size_t local_index;
	size_t text_length;

	if (out_index != NULL) {
		*out_index = 0U;
	}
	if (build == NULL || build->cache == NULL || build->statement == NULL ||
	    source == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "query graph builder or value source is missing");
		return -1;
	}
	if (source->has_selector) {
		size_t index;

		if (source->selector.kind != SQLPARSER_SELECTOR_KIND_VALUE ||
		    source->selector.statement_index != build->statement_index ||
		    source->selector.row_index != 0U ||
		    source->selector.column_index != 0U) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "query graph value selector is invalid");
			return -1;
		}
		for (index = build->statement->value_offset;
		     index < build->cache->value_count;
		     index++) {
			sqlparser_graph_value_cache_t *existing;

			existing = &build->cache->values[index];
			if ((existing->flags &
			     SQLPARSER_GRAPH_VALUE_HAS_SELECTOR) != 0U &&
			    existing->selector_item_index ==
				    source->selector.item_index &&
			    existing->kind == (uint8_t)source->kind &&
			    ((existing->flags &
			      SQLPARSER_GRAPH_VALUE_HAS_FIELD) != 0U) ==
				    (source->has_field != 0) &&
			    (!source->has_field || existing->field_index == source->field_index) &&
			    ((existing->flags &
			      SQLPARSER_GRAPH_VALUE_HAS_SOURCE_FIELD) != 0U) ==
				    (source->has_source_field != 0) &&
			    (!source->has_source_field || existing->source_field_index == source->source_field_index) &&
			    existing->field_match_kind ==
				    (uint8_t)source->field_match_kind) {
				if (out_index != NULL) {
					*out_index =
						index - build->statement->value_offset;
				}
				return 0;
			}
		}
	}
	if (sqlparser_query_graph_reserve_array_with_initial(
		    (void **)&build->cache->values,
		    &build->cache->value_capacity,
		    build->cache->value_count + 1U,
		    sizeof(*build->cache->values),
		    4U,
		    out_error) != 0) {
		return -1;
	}
	if (source->like_escape.kind != SQLPARSER_GRAPH_LIKE_ESCAPE_NONE &&
	    sqlparser_query_graph_reserve_sparse_array(
		    (void **)&build->cache->like_escapes,
		    &build->cache->like_escape_capacity,
		    build->cache->like_escape_count + 1U,
		    sizeof(*build->cache->like_escapes),
		    out_error) != 0) {
		return -1;
	}
	global_index = build->cache->value_count;
	local_index = sqlparser_graph_local_value_count(build);
	text_length = build->cache->value_text_length;
	if (sqlparser_graph_value_cache_init(
		    build->cache,
		    &value,
		    source,
		    out_error) != 0) {
		return -1;
	}
	if (source->like_escape.kind != SQLPARSER_GRAPH_LIKE_ESCAPE_NONE &&
	    sqlparser_graph_like_escape_cache_init(
		    build->cache,
		    &escape,
		    global_index,
		    &source->like_escape,
		    out_error) != 0) {
		build->cache->value_text_length = text_length;
		return -1;
	}
	build->cache->values[global_index] = value;
	if (source->like_escape.kind != SQLPARSER_GRAPH_LIKE_ESCAPE_NONE) {
		build->cache->like_escapes[build->cache->like_escape_count++] =
			escape;
	}
	build->cache->value_count++;
	if (out_index != NULL) {
		*out_index = local_index;
	}
	return 0;
}

static int sqlparser_graph_add_predicate(
	sqlparser_graph_build_t *build,
	const sqlparser_graph_predicate_t *source,
	size_t *out_index,
	sqlparser_error_t *out_error)
{
	sqlparser_graph_predicate_t *predicate;
	size_t global_index;
	size_t local_index;

	if (out_index != NULL) {
		*out_index = 0U;
	}
	if (sqlparser_query_graph_reserve_array(
		    (void **)&build->cache->predicates,
		    &build->cache->predicate_capacity,
		    build->cache->predicate_count + 1U,
		    sizeof(*build->cache->predicates),
		    out_error) != 0) {
		return -1;
	}
	global_index = build->cache->predicate_count++;
	local_index = sqlparser_graph_local_predicate_count(build) - 1U;
	predicate = &build->cache->predicates[global_index];
	memset(predicate, 0, sizeof(*predicate));
	if (source != NULL) {
		*predicate = *source;
	}
	predicate->index = local_index;
	predicate->statement_index = build->statement_index;
	if (out_index != NULL) {
		*out_index = local_index;
	}
	return 0;
}

static int sqlparser_graph_add_target_value_from_node(
	sqlparser_graph_build_t *build,
	size_t block_index,
	sqlparser_clause_kind_t clause,
	PgQuery__Node *value_node,
	size_t *out_value_index,
	int *out_added,
	sqlparser_error_t *out_error)
{
	sqlparser_graph_value_t value;
	int value_status;

	if (out_value_index != NULL) {
		*out_value_index = 0U;
	}
	if (out_added != NULL) {
		*out_added = 0;
	}
	memset(&value, 0, sizeof(value));
	value_status = sqlparser_graph_value_from_node(
		build,
		block_index,
		clause,
		NULL,
		0U,
		0,
		SQLPARSER_GRAPH_FIELD_MATCH_UNKNOWN,
		value_node,
		NULL,
		&value,
		out_error);
	if (value_status < 0) {
		return -1;
	}
	if (value_status == 0) {
		return 0;
	}
	if (sqlparser_graph_add_value(
		    build, &value, out_value_index, out_error) != 0) {
		return -1;
	}
	if (out_added != NULL) {
		*out_added = 1;
	}
	return 0;
}

static int sqlparser_graph_add_set(
	sqlparser_graph_build_t *build,
	const sqlparser_graph_set_t *source,
	size_t *out_index,
	sqlparser_error_t *out_error)
{
	sqlparser_graph_set_t *set_item;
	size_t global_index;
	size_t local_index;

	if (out_index != NULL) {
		*out_index = 0U;
	}
	if (sqlparser_query_graph_reserve_array(
		    (void **)&build->cache->sets,
		    &build->cache->set_capacity,
		    build->cache->set_count + 1U,
		    sizeof(*build->cache->sets),
		    out_error) != 0) {
		return -1;
	}
	global_index = build->cache->set_count++;
	local_index = sqlparser_graph_local_set_count(build) - 1U;
	set_item = &build->cache->sets[global_index];
	memset(set_item, 0, sizeof(*set_item));
	if (source != NULL) {
		*set_item = *source;
	}
	set_item->index = local_index;
	set_item->statement_index = build->statement_index;
	if (out_index != NULL) {
		*out_index = local_index;
	}
	return 0;
}

static char **sqlparser_graph_column_ref_name_slot(PgQuery__ColumnRef *column_ref)
{
	PgQuery__Node *field;

	if (column_ref == NULL || column_ref->n_fields == 0U) {
		return NULL;
	}
	field = column_ref->fields[column_ref->n_fields - 1U];
	if (field == NULL || field->node_case != PG_QUERY__NODE__NODE_STRING || field->string == NULL) {
		return NULL;
	}
	return &field->string->sval;
}

static const char *sqlparser_graph_column_ref_part(PgQuery__ColumnRef *column_ref, size_t reverse_index)
{
	size_t text_seen;
	size_t index;

	if (column_ref == NULL) {
		return NULL;
	}
	text_seen = 0U;
	for (index = column_ref->n_fields; index > 0U; index--) {
		const char *text;

		text = NULL;
		if (sqlparser_node_string_value(column_ref->fields[index - 1U], &text)) {
			if (text_seen == reverse_index) {
				return text;
			}
			text_seen++;
		} else if (column_ref->fields[index - 1U] != NULL &&
			   column_ref->fields[index - 1U]->node_case == PG_QUERY__NODE__NODE_A_STAR) {
			if (text_seen == reverse_index) {
				return "*";
			}
			text_seen++;
		}
	}
	return NULL;
}

static sqlparser_identifier_source_t sqlparser_graph_column_ref_part_source(
	const sqlparser_handle_t *handle,
	PgQuery__ColumnRef *column_ref,
	size_t reverse_index)
{
	sqlparser_identifier_source_t source;
	size_t component_index;
	size_t part_count;
	size_t index;

	memset(&source, 0, sizeof(source));
	if (column_ref == NULL) {
		return source;
	}
	part_count = 0U;
	for (index = 0U; index < column_ref->n_fields; index++) {
		const char *text;

		text = NULL;
		if (sqlparser_node_string_value(column_ref->fields[index], &text) ||
		    (column_ref->fields[index] != NULL &&
		     column_ref->fields[index]->node_case ==
			     PG_QUERY__NODE__NODE_A_STAR)) {
			part_count++;
		}
	}
	if (reverse_index >= part_count) {
		return source;
	}
	component_index = part_count - reverse_index - 1U;
	part_count = 0U;
	for (index = 0U; index < column_ref->n_fields; index++) {
		PgQuery__Node *field;
		const char *text;

		field = column_ref->fields[index];
		text = NULL;
		if (!sqlparser_node_string_value(field, &text) &&
		    (field == NULL ||
		     field->node_case != PG_QUERY__NODE__NODE_A_STAR)) {
			continue;
		}
		if (part_count++ != component_index) {
			continue;
		}
		if (field != NULL &&
		    field->node_case == PG_QUERY__NODE__NODE_STRING &&
		    field->string != NULL) {
			(void)sqlparser_current_identifier_source(
				handle,
				(const char *const *)&field->string->sval,
				column_ref->location,
				component_index,
				&source);
		} else {
			(void)sqlparser_identifier_component_source(
				handle,
				column_ref->location,
				component_index,
				&source,
				NULL);
		}
		break;
	}
	return source;
}

static int sqlparser_graph_column_ref_is_hierarchy_pseudo(
	const sqlparser_graph_build_t *build,
	PgQuery__ColumnRef *column_ref)
{
	sqlparser_identifier_source_t source;
	const char *name;
	const char *qualifier;

	if (build == NULL || build->handle == NULL ||
	    !build->hierarchy_query_active ||
	    !sqlparser_dialect_is_oracle_or_dameng_compatible(
		    build->handle->dialect)) {
		return 0;
	}
	name = sqlparser_graph_column_ref_part(column_ref, 0U);
	qualifier = sqlparser_graph_column_ref_part(column_ref, 1U);
	if (name == NULL) {
		return 0;
	}
	source = sqlparser_graph_column_ref_part_source(
		build->handle,
		column_ref,
		0U);
	if (source.known && source.quoted) {
		return 0;
	}
	return (qualifier == NULL || qualifier[0] == '\0') &&
		(sqlparser_text_equal_ci(name, "level") ||
		 sqlparser_text_equal_ci(name, "connect_by_isleaf") ||
		 sqlparser_text_equal_ci(name, "connect_by_iscycle"));
}

static int sqlparser_graph_column_ref_is_pseudo(
	const sqlparser_graph_build_t *build,
	PgQuery__ColumnRef *column_ref)
{
	sqlparser_identifier_source_t source;
	const char *name;
	const char *qualifier;

	if (build == NULL || build->handle == NULL ||
	    !sqlparser_dialect_is_oracle_or_dameng_compatible(
		    build->handle->dialect)) {
		return 0;
	}
	name = sqlparser_graph_column_ref_part(column_ref, 0U);
	qualifier = sqlparser_graph_column_ref_part(column_ref, 1U);
	if (name == NULL) {
		return 0;
	}
	source = sqlparser_graph_column_ref_part_source(
		build->handle,
		column_ref,
		0U);
	if (source.known && source.quoted) {
		return 0;
	}
	if (sqlparser_text_equal_ci(name, "rowid")) {
		return 1;
	}
	return ((qualifier == NULL || qualifier[0] == '\0') &&
		sqlparser_text_equal_ci(name, "rownum")) ||
		sqlparser_graph_column_ref_is_hierarchy_pseudo(build, column_ref);
}

static int sqlparser_graph_column_ref_is_dialect_expression_name(
	const sqlparser_graph_build_t *build,
	PgQuery__ColumnRef *column_ref)
{
	const char *name;
	const char *qualifier;

	if (build == NULL || build->handle == NULL ||
	    !sqlparser_dialect_is_oracle_or_dameng_compatible(
		    build->handle->dialect)) {
		return 0;
	}
	name = sqlparser_graph_column_ref_part(column_ref, 0U);
	qualifier = sqlparser_graph_column_ref_part(column_ref, 1U);
	if (name == NULL ||
	    (qualifier != NULL && qualifier[0] != '\0') ||
	    !sqlparser_text_equal_ci(name, "sysdate")) {
		return 0;
	}
	return 1;
}

static int sqlparser_graph_column_ref_is_dialect_expression(
	const sqlparser_graph_build_t *build,
	PgQuery__ColumnRef *column_ref)
{
	sqlparser_identifier_source_t source;

	if (!sqlparser_graph_column_ref_is_dialect_expression_name(
		    build,
		    column_ref)) {
		return 0;
	}
	source = sqlparser_graph_column_ref_part_source(
		build->handle,
		column_ref,
		0U);
	if (source.known) {
		return !source.quoted;
	}
	return column_ref->location == SQLPARSER_PROTO_LOCATION_GENERATED;
}

static sqlparser_identifier_source_t
sqlparser_graph_fragment_identifier_source(const char *sql)
{
	sqlparser_identifier_source_t source;
	size_t length;
	size_t position;
	size_t token_end;
	int quoted;

	memset(&source, 0, sizeof(source));
	if (sql != NULL) {
		length = strlen(sql);
		position = 0U;
		for (;;) {
			position = sqlparser_identifier_skip_trivia(
				sql,
				length,
				position);
			if (position >= length || sql[position] != '(') {
				break;
			}
			position++;
		}
		if (sqlparser_identifier_token(
			    sql,
			    length,
			    position,
			    &token_end,
			    &quoted)) {
			source.known = 1;
			source.quoted = quoted;
		}
	}
	return source;
}

static sqlparser_identifier_source_t
sqlparser_graph_relation_object_source(const char *sql)
{
	sqlparser_identifier_source_t source;
	size_t length;
	size_t position;

	memset(&source, 0, sizeof(source));
	if (sql == NULL) {
		return source;
	}
	length = strlen(sql);
	position = 0U;
	for (;;) {
		size_t token_end;
		int quoted;

		position = sqlparser_identifier_skip_trivia(
			sql,
			length,
			position);
		if (!sqlparser_identifier_token(
			    sql,
			    length,
			    position,
			    &token_end,
			    &quoted)) {
			memset(&source, 0, sizeof(source));
			return source;
		}
		source.known = 1;
		source.quoted = quoted;
		source.delimited =
			sqlparser_identifier_spelling_is_delimited(
				sql + position,
				token_end - position);
		position = sqlparser_identifier_skip_trivia(
			sql,
			length,
			token_end);
		if (position == length) {
			return source;
		}
		if (sql[position] != '.') {
			memset(&source, 0, sizeof(source));
			return source;
		}
		position++;
	}
}

static int sqlparser_graph_fragment_column_ref_is_dialect_expression(
	const sqlparser_graph_build_t *build,
	PgQuery__ColumnRef *column_ref,
	const char *sql)
{
	sqlparser_identifier_source_t source;

	if (!sqlparser_graph_column_ref_is_dialect_expression_name(
		    build,
		    column_ref)) {
		return 0;
	}
	source = sqlparser_graph_fragment_identifier_source(sql);
	return source.known && !source.quoted;
}

static int sqlparser_graph_column_ref_is_recordable_field(
	const sqlparser_graph_build_t *build,
	PgQuery__ColumnRef *column_ref)
{
	const char *name;

	if (column_ref == NULL ||
	    sqlparser_graph_column_ref_is_dialect_expression(build, column_ref) ||
	    (sqlparser_graph_column_ref_is_pseudo(build, column_ref) &&
	     !sqlparser_graph_column_ref_is_hierarchy_pseudo(build, column_ref))) {
		return 0;
	}
	name = sqlparser_graph_column_ref_part(column_ref, 0U);
	return name != NULL && name[0] != '\0' && strcmp(name, "*") != 0;
}

static int sqlparser_graph_column_ref_qualifier_count(
	const PgQuery__ColumnRef *column_ref,
	size_t *out_count);
static int sqlparser_graph_relation_matches_column_ref_path(
	sqlparser_graph_build_t *build,
	size_t relation_index,
	PgQuery__ColumnRef *column_ref,
	size_t qualifier_count,
	const sqlparser_graph_relation_identifier_t *reference_identifiers,
	int *out_used_alias);
static int sqlparser_graph_match_relation_path(
	sqlparser_graph_build_t *build,
	size_t block_index,
	PgQuery__ColumnRef *column_ref,
	size_t qualifier_count,
	size_t *out_match_count,
	size_t *out_relation_index,
	int *out_used_alias,
	sqlparser_index_span_t *out_candidates,
	sqlparser_error_t *out_error);
static int sqlparser_graph_collect_resolved_column_ref(
	sqlparser_graph_build_t *build,
	PgQuery__ColumnRef *column_ref,
	size_t match_count,
	size_t relation_index,
	int used_alias,
	sqlparser_error_t *out_error);

static int sqlparser_graph_relation_matches(
	sqlparser_graph_build_t *build,
	size_t relation_index,
	const sqlparser_graph_relation_t *relation,
	const char *qualifier,
	PgQuery__ColumnRef *column_ref)
{
	sqlparser_graph_relation_identifier_t *identifiers;
	sqlparser_identifier_source_t alias_source;
	sqlparser_identifier_source_t object_source;
	sqlparser_identifier_source_t qualifier_source;

	if (relation == NULL || qualifier == NULL || qualifier[0] == '\0') {
		return 0;
	}
	memset(&alias_source, 0, sizeof(alias_source));
	memset(&object_source, 0, sizeof(object_source));
	identifiers = sqlparser_graph_relation_identifier_by_local(
		build,
		relation_index);
	if (identifiers != NULL) {
		alias_source = identifiers->alias;
		object_source = identifiers->object;
	}
	qualifier_source = sqlparser_graph_column_ref_part_source(
		build != NULL ? build->handle : NULL,
		column_ref,
		1U);
	if (relation->alias_name != NULL &&
	    sqlparser_identifier_semantic_equal(
		    build != NULL ? build->handle : NULL,
		    relation->alias_name,
		    alias_source,
		    qualifier,
		    qualifier_source)) {
		return 1;
	}
	return relation->object_name != NULL &&
		sqlparser_identifier_semantic_equal(
			build != NULL ? build->handle : NULL,
			relation->object_name,
			object_source,
			qualifier,
			qualifier_source);
}

static int sqlparser_graph_column_ref_is_on_conflict_excluded(
	const sqlparser_graph_build_t *build,
	PgQuery__ColumnRef *column_ref)
{
	const sqlparser_identifier_mutation_t *mutation;
	sqlparser_identifier_source_t pseudo_source;
	sqlparser_identifier_source_t qualifier_source;
	PgQuery__Node *qualifier_node;
	const char *qualifier;
	size_t qualifier_count;

	qualifier_count = 0U;
	if (build == NULL || !build->in_on_conflict_update ||
	    build->handle == NULL ||
	    (build->handle->dialect != SQLPARSER_DIALECT_POSTGRESQL &&
	     build->handle->dialect != SQLPARSER_DIALECT_VASTBASE_POSTGRESQL) ||
	    !sqlparser_graph_column_ref_qualifier_count(
		    column_ref,
		    &qualifier_count) ||
	    qualifier_count != 1U) {
		return 0;
	}
	qualifier = sqlparser_graph_column_ref_part(column_ref, 1U);
	qualifier_source = sqlparser_graph_column_ref_part_source(
		build->handle,
		column_ref,
		1U);
	qualifier_node = column_ref->fields[0];
	mutation = qualifier_node != NULL &&
		    qualifier_node->node_case == PG_QUERY__NODE__NODE_STRING &&
		    qualifier_node->string != NULL ?
		sqlparser_identifier_mutation_for_source_slot(
			build->handle,
			(const char *const *)&qualifier_node->string->sval) :
		NULL;
	if (mutation != NULL &&
	    (!mutation->source_present ||
	     (mutation->original != NULL &&
	      strcmp(mutation->original, qualifier) != 0))) {
		return 0;
	}
	memset(&pseudo_source, 0, sizeof(pseudo_source));
	pseudo_source.known = 1;
	return sqlparser_identifier_semantic_equal(
		build->handle,
		"excluded",
		pseudo_source,
		qualifier,
		qualifier_source);
}

static int sqlparser_graph_resolve_relation(
	sqlparser_graph_build_t *build,
	size_t block_index,
	const char *qualifier,
	PgQuery__ColumnRef *column_ref,
	size_t *out_relation_index,
	int *out_has_relation,
	sqlparser_index_span_t *out_candidates,
	sqlparser_error_t *out_error)
{
	sqlparser_graph_scope_t *scope;
	int is_on_conflict_excluded;

	if (out_relation_index != NULL) {
		*out_relation_index = 0U;
	}
	if (out_has_relation != NULL) {
		*out_has_relation = 0;
	}
	if (out_candidates != NULL) {
		memset(out_candidates, 0, sizeof(*out_candidates));
	}
	is_on_conflict_excluded =
		sqlparser_graph_column_ref_is_on_conflict_excluded(
			build,
			column_ref);
	if (qualifier != NULL && qualifier[0] != '\0') {
		size_t qualifier_count;
		int has_qualifier_path;

		qualifier_count = 0U;
		has_qualifier_path =
			sqlparser_graph_column_ref_qualifier_count(
				column_ref,
				&qualifier_count);
		for (scope = build->scope; scope != NULL; scope = scope->parent) {
			size_t scope_block;
			size_t match_count;
			size_t matched_relation_index;
			int matched_using_alias;
			size_t index;

			scope_block = scope->block_index;
			if (is_on_conflict_excluded &&
			    scope_block ==
				    build->on_conflict_target_block_index) {
				return 0;
			}
			if (has_qualifier_path) {
				if (sqlparser_graph_match_relation_path(
					    build,
					    scope_block,
					    column_ref,
					    qualifier_count,
					    &match_count,
					    &matched_relation_index,
					    &matched_using_alias,
					    out_candidates,
					    out_error) != 0) {
					return -1;
				}
				if (match_count == 0U) {
					continue;
				}
				if (match_count != 1U) {
					return 0;
				}
				if (out_relation_index != NULL) {
					*out_relation_index =
						matched_relation_index;
				}
				if (out_has_relation != NULL) {
					*out_has_relation = 1;
				}
				return sqlparser_graph_collect_resolved_column_ref(
					build,
					column_ref,
					match_count,
					matched_relation_index,
					matched_using_alias,
					out_error);
			}
			for (index = 0U; index < sqlparser_graph_local_relation_count(build); index++) {
				sqlparser_graph_relation_t *relation;

				relation = sqlparser_graph_relation_by_local(build, index);
				if (relation != NULL &&
				    relation->block_index == scope_block &&
				    sqlparser_graph_relation_matches(
					    build,
					    index,
					    relation,
					    qualifier,
					    column_ref)) {
					if (out_relation_index != NULL) {
						*out_relation_index = index;
					}
					if (out_has_relation != NULL) {
						*out_has_relation = 1;
					}
					return 0;
				}
			}
		}
		if (build->scope == NULL && has_qualifier_path) {
			size_t match_count;
			size_t matched_relation_index;
			int matched_using_alias;

			if (is_on_conflict_excluded &&
			    block_index ==
				    build->on_conflict_target_block_index) {
				return 0;
			}
			if (sqlparser_graph_match_relation_path(
				    build,
				    block_index,
				    column_ref,
				    qualifier_count,
				    &match_count,
				    &matched_relation_index,
				    &matched_using_alias,
				    out_candidates,
				    out_error) != 0) {
				return -1;
			}
			if (match_count == 1U) {
				if (out_relation_index != NULL) {
					*out_relation_index =
						matched_relation_index;
				}
				if (out_has_relation != NULL) {
					*out_has_relation = 1;
				}
				return sqlparser_graph_collect_resolved_column_ref(
					build,
					column_ref,
					match_count,
					matched_relation_index,
					matched_using_alias,
					out_error);
			}
		}
		return 0;
	}
	{
		size_t count;
		size_t only_index;
		size_t index;
		size_t scope_block;
		int explicit_block_pending;

		explicit_block_pending = build->scope == NULL;
		for (scope = build->scope;
		     scope != NULL || explicit_block_pending;
		     scope = scope != NULL ? scope->parent : NULL) {
			scope_block = scope != NULL ? scope->block_index : block_index;
			explicit_block_pending = 0;
			count = 0U;
			only_index = 0U;
			for (index = 0U;
			     index < sqlparser_graph_local_relation_count(build);
			     index++) {
				sqlparser_graph_relation_t *relation;

				relation = sqlparser_graph_relation_by_local(build, index);
				if (relation == NULL ||
				    relation->block_index != scope_block) {
					continue;
				}
				only_index = index;
				count++;
				if (out_candidates != NULL &&
				    sqlparser_graph_span_append_index(
					    build,
					    out_candidates,
					    index,
					    out_error) != 0) {
					return -1;
				}
			}
			if (count == 0U) {
				continue;
			}
			if (count == 1U) {
				if (out_relation_index != NULL) {
					*out_relation_index = only_index;
				}
				if (out_has_relation != NULL) {
					*out_has_relation = 1;
				}
				if (out_candidates != NULL) {
					memset(
						out_candidates,
						0,
						sizeof(*out_candidates));
				}
			}
			return 0;
		}
	}
	return 0;
}

static PgQuery__RangeVar *sqlparser_graph_relation_range_var(
	sqlparser_graph_build_t *build,
	const sqlparser_graph_relation_t *relation);

static void sqlparser_graph_collect_register_relation(
	sqlparser_graph_build_t *build,
	size_t relation_index,
	const sqlparser_graph_relation_t *relation)
{
	if (build == NULL || !build->collect_relation_bindings ||
	    relation == NULL || !relation->has_selector ||
	    relation->selector.kind != SQLPARSER_SELECTOR_KIND_RELATION ||
	    relation->selector.statement_index != build->statement_index ||
	    sqlparser_graph_relation_range_var(build, relation) !=
		    build->collect_relation) {
		return;
	}
	if (build->collect_has_target_relation &&
	    build->collect_target_relation_index != relation_index) {
		build->collect_target_relation_ambiguous = 1;
		return;
	}
	build->collect_target_relation_index = relation_index;
	build->collect_has_target_relation = 1;
}

static int sqlparser_graph_collect_append_column_ref(
	sqlparser_graph_build_t *build,
	PgQuery__ColumnRef *column_ref,
	sqlparser_error_t *out_error)
{
	if (sqlparser_query_graph_reserve_sparse_array(
		    (void **)&build->collected_column_refs,
		    &build->collected_column_ref_capacity,
		    build->collected_column_ref_count + 1U,
		    sizeof(*build->collected_column_refs),
		    out_error) != 0) {
		return -1;
	}
	build->collected_column_refs[
		build->collected_column_ref_count++] = column_ref;
	return 0;
}

static int sqlparser_graph_collect_append_assignment_target(
	sqlparser_graph_build_t *build,
	PgQuery__ResTarget *target,
	sqlparser_error_t *out_error)
{
	if (sqlparser_query_graph_reserve_sparse_array(
		    (void **)&build->collected_assignment_targets,
		    &build->collected_assignment_target_capacity,
		    build->collected_assignment_target_count + 1U,
		    sizeof(*build->collected_assignment_targets),
		    out_error) != 0) {
		return -1;
	}
	build->collected_assignment_targets[
		build->collected_assignment_target_count++] = target;
	return 0;
}

static PgQuery__RangeVar *sqlparser_graph_relation_range_var(
	sqlparser_graph_build_t *build,
	const sqlparser_graph_relation_t *relation)
{
	size_t selector_index;

	if (build == NULL || relation == NULL || !relation->has_selector ||
	    relation->selector.kind != SQLPARSER_SELECTOR_KIND_RELATION ||
	    relation->selector.statement_index != build->statement_index ||
	    sqlparser_graph_ensure_selector_cache(build, NULL) != 0) {
		return NULL;
	}
	selector_index = relation->selector.item_index;
	if (selector_index >= build->relation_index_count ||
	    build->relation_indices == NULL) {
		return NULL;
	}
	return (PgQuery__RangeVar *)
		build->relation_indices[selector_index].pointer;
}

static int sqlparser_graph_relation_path_part(
	sqlparser_graph_build_t *build,
	size_t relation_index,
	size_t reverse_index,
	const char **out_text,
	sqlparser_identifier_source_t *out_source)
{
	sqlparser_graph_relation_identifier_t *identifiers;
	sqlparser_graph_relation_t *relation;

	if (out_text != NULL) {
		*out_text = NULL;
	}
	if (out_source != NULL) {
		memset(out_source, 0, sizeof(*out_source));
	}
	relation = sqlparser_graph_relation_by_local(build, relation_index);
	if (relation == NULL || out_text == NULL || out_source == NULL) {
		return 0;
	}
	identifiers = sqlparser_graph_relation_identifier_by_local(
		build,
		relation_index);
	switch (reverse_index) {
		case 0U:
			*out_text = relation->object_name;
			if (identifiers != NULL) {
				*out_source = identifiers->object;
			}
			break;
		case 1U:
			*out_text = relation->schema_name;
			if (identifiers != NULL) {
				*out_source = identifiers->schema;
			}
			break;
		case 2U:
			*out_text = relation->database_name;
			if (identifiers != NULL) {
				*out_source = identifiers->database;
			}
			break;
		default:
			return 0;
	}
	return *out_text != NULL && (*out_text)[0] != '\0';
}

static int sqlparser_graph_column_ref_qualifier_count(
	const PgQuery__ColumnRef *column_ref,
	size_t *out_count)
{
	size_t index;

	if (out_count != NULL) {
		*out_count = 0U;
	}
	if (column_ref == NULL || column_ref->fields == NULL ||
	    column_ref->n_fields < 2U || out_count == NULL) {
		return 0;
	}
	for (index = 0U; index < column_ref->n_fields; index++) {
		PgQuery__Node *field;

		field = column_ref->fields[index];
		if (field == NULL) {
			return 0;
		}
		if (field->node_case == PG_QUERY__NODE__NODE_STRING &&
		    field->string != NULL && field->string->sval != NULL &&
		    field->string->sval[0] != '\0') {
			continue;
		}
		if (index + 1U == column_ref->n_fields &&
		    field->node_case == PG_QUERY__NODE__NODE_A_STAR &&
		    field->a_star != NULL) {
			continue;
		}
		return 0;
	}
	*out_count = column_ref->n_fields - 1U;
	return 1;
}

static int sqlparser_graph_relation_matches_column_ref_path(
	sqlparser_graph_build_t *build,
	size_t relation_index,
	PgQuery__ColumnRef *column_ref,
	size_t qualifier_count,
	const sqlparser_graph_relation_identifier_t *reference_identifiers,
	int *out_used_alias)
{
	sqlparser_graph_relation_identifier_t *identifiers;
	sqlparser_graph_relation_t *relation;
	sqlparser_identifier_source_t reference_source;
	sqlparser_identifier_source_t relation_source;
	const char *reference_part;
	const char *relation_part;
	size_t index;

	if (out_used_alias != NULL) {
		*out_used_alias = 0;
	}
	relation = sqlparser_graph_relation_by_local(build, relation_index);
	if (relation == NULL || qualifier_count == 0U ||
	    reference_identifiers == NULL) {
		return 0;
	}
	identifiers = sqlparser_graph_relation_identifier_by_local(
		build,
		relation_index);
	if (relation->alias_name != NULL &&
	    relation->alias_name[0] != '\0') {
		if (qualifier_count != 1U) {
			return 0;
		}
		reference_part = sqlparser_graph_column_ref_part(
			column_ref,
			1U);
		reference_source = reference_identifiers->object;
		memset(&relation_source, 0, sizeof(relation_source));
		if (identifiers != NULL) {
			relation_source = identifiers->alias;
		}
		if (!sqlparser_identifier_semantic_equal(
			    build->handle,
			    relation->alias_name,
			    relation_source,
			    reference_part,
			    reference_source)) {
			return 0;
		}
		if (out_used_alias != NULL) {
			*out_used_alias = 1;
		}
		return 1;
	}
	for (index = 0U; index < qualifier_count; index++) {
		reference_part = sqlparser_graph_column_ref_part(
			column_ref,
			index + 1U);
		switch (index) {
			case 0U:
				reference_source = reference_identifiers->object;
				break;
			case 1U:
				reference_source = reference_identifiers->schema;
				break;
			case 2U:
				reference_source = reference_identifiers->database;
				break;
			default:
				return 0;
		}
		if (!sqlparser_graph_relation_path_part(
			    build,
			    relation_index,
			    index,
			    &relation_part,
			    &relation_source) ||
		    !sqlparser_identifier_semantic_equal(
			    build->handle,
			    relation_part,
			    relation_source,
			    reference_part,
			    reference_source)) {
			return 0;
		}
	}
	return 1;
}

static int sqlparser_graph_match_relation_path(
	sqlparser_graph_build_t *build,
	size_t block_index,
	PgQuery__ColumnRef *column_ref,
	size_t qualifier_count,
	size_t *out_match_count,
	size_t *out_relation_index,
	int *out_used_alias,
	sqlparser_index_span_t *out_candidates,
	sqlparser_error_t *out_error)
{
	sqlparser_graph_relation_identifier_t reference_identifiers;
	size_t first_relation_index;
	size_t match_count;
	size_t relation_index;

	if (out_match_count != NULL) {
		*out_match_count = 0U;
	}
	if (out_relation_index != NULL) {
		*out_relation_index = 0U;
	}
	if (out_used_alias != NULL) {
		*out_used_alias = 0;
	}
	if (build == NULL || column_ref == NULL ||
	    qualifier_count == 0U || qualifier_count > 3U) {
		return 0;
	}
	memset(&reference_identifiers, 0, sizeof(reference_identifiers));
	reference_identifiers.object = sqlparser_graph_column_ref_part_source(
		build->handle,
		column_ref,
		1U);
	if (qualifier_count > 1U) {
		reference_identifiers.schema =
			sqlparser_graph_column_ref_part_source(
				build->handle,
				column_ref,
				2U);
	}
	if (qualifier_count > 2U) {
		reference_identifiers.database =
			sqlparser_graph_column_ref_part_source(
				build->handle,
				column_ref,
				3U);
	}
	if (build->handle != NULL &&
	    sqlparser_dialect_is_sqlserver_compatible(
		    build->handle->dialect)) {
		size_t dml_global_index;

		for (dml_global_index = build->statement->dml_offset;
		     dml_global_index < build->cache->dml_count;
		     dml_global_index++) {
			sqlparser_graph_dml_t *dml;
			sqlparser_graph_relation_t *relation;
			int used_alias;

			dml = &build->cache->dml[dml_global_index];
			if (dml->kind != SQLPARSER_GRAPH_DML_UPDATE ||
			    !dml->has_target_relation) {
				continue;
			}
			relation = sqlparser_graph_relation_by_local(
				build,
				dml->target_relation_index);
			used_alias = 0;
			if (relation == NULL || relation->block_index != block_index ||
			    !sqlparser_graph_relation_matches_column_ref_path(
				    build,
				    dml->target_relation_index,
				    column_ref,
				    qualifier_count,
				    &reference_identifiers,
				    &used_alias)) {
				continue;
			}
			for (relation_index = 0U;
			     relation_index <
				     sqlparser_graph_local_relation_count(build);
			     relation_index++) {
				int matched_alias;

				if (relation_index ==
				    dml->target_relation_index) {
					continue;
				}
				relation = sqlparser_graph_relation_by_local(
					build,
					relation_index);
				matched_alias = 0;
				if (relation != NULL &&
				    relation->block_index == block_index &&
				    sqlparser_graph_relation_matches_column_ref_path(
					    build,
					    relation_index,
					    column_ref,
					    qualifier_count,
					    &reference_identifiers,
					    &matched_alias) &&
				    matched_alias) {
					used_alias = 1;
					break;
				}
			}
			if (out_match_count != NULL) {
				*out_match_count = 1U;
			}
			if (out_relation_index != NULL) {
				*out_relation_index =
					dml->target_relation_index;
			}
			if (out_used_alias != NULL) {
				*out_used_alias = used_alias;
			}
			return 0;
		}
	}
	first_relation_index = 0U;
	match_count = 0U;
	for (relation_index = 0U;
	     relation_index < sqlparser_graph_local_relation_count(build);
	     relation_index++) {
		sqlparser_graph_relation_t *relation;
		int used_alias;

		relation = sqlparser_graph_relation_by_local(
			build,
			relation_index);
		used_alias = 0;
		if (relation == NULL || relation->block_index != block_index ||
		    !sqlparser_graph_relation_matches_column_ref_path(
			    build,
			    relation_index,
			    column_ref,
			    qualifier_count,
			    &reference_identifiers,
			    &used_alias)) {
			continue;
		}
		if (match_count == 0U) {
			first_relation_index = relation_index;
			if (out_relation_index != NULL) {
				*out_relation_index = relation_index;
			}
			if (out_used_alias != NULL) {
				*out_used_alias = used_alias;
			}
		} else if (out_candidates != NULL) {
			if (match_count == 1U &&
			    sqlparser_graph_span_append_index(
				    build,
				    out_candidates,
				    first_relation_index,
				    out_error) != 0) {
				return -1;
			}
			if (sqlparser_graph_span_append_index(
				    build,
				    out_candidates,
				    relation_index,
				    out_error) != 0) {
				return -1;
			}
		}
		match_count++;
	}
	if (out_match_count != NULL) {
		*out_match_count = match_count;
	}
	return 0;
}

static int sqlparser_graph_collect_resolved_column_ref(
	sqlparser_graph_build_t *build,
	PgQuery__ColumnRef *column_ref,
	size_t match_count,
	size_t relation_index,
	int used_alias,
	sqlparser_error_t *out_error)
{
	const char *qualifier;

	if (build == NULL || !build->collect_relation_bindings ||
	    !build->collect_has_target_relation ||
	    build->collect_target_relation_ambiguous ||
	    match_count != 1U ||
	    relation_index != build->collect_target_relation_index ||
	    used_alias) {
		return 0;
	}
	qualifier = sqlparser_graph_column_ref_part(column_ref, 1U);
	if (build->building_dml_result &&
	    build->handle != NULL &&
	    sqlparser_dialect_is_sqlserver_compatible(
		    build->handle->dialect) &&
	    qualifier != NULL &&
	    (sqlparser_text_equal_ci(qualifier, "inserted") ||
	     sqlparser_text_equal_ci(qualifier, "deleted"))) {
		return 0;
	}
	return sqlparser_graph_collect_append_column_ref(
		build,
		column_ref,
		out_error);
}

static const char *sqlparser_graph_res_target_path_part(
	const PgQuery__ResTarget *target,
	size_t index)
{
	const char *text;
	PgQuery__Node *node;

	if (target == NULL) {
		return NULL;
	}
	if (index == 0U) {
		return target->name;
	}
	if (target->indirection == NULL ||
	    index - 1U >= target->n_indirection) {
		return NULL;
	}
	node = target->indirection[index - 1U];
	text = NULL;
	return sqlparser_node_string_value(node, &text) &&
		text != NULL && text[0] != '\0' ? text : NULL;
}

static int sqlparser_graph_res_target_path_source(
	sqlparser_graph_build_t *build,
	const PgQuery__ResTarget *target,
	size_t index,
	sqlparser_identifier_source_t *out_source)
{
	PgQuery__Node *node;

	if (out_source != NULL) {
		memset(out_source, 0, sizeof(*out_source));
	}
	if (build == NULL || target == NULL || out_source == NULL) {
		return 0;
	}
	if (index == 0U) {
		return sqlparser_current_identifier_source(
			build->handle,
			(const char *const *)&target->name,
			target->location,
			0U,
			out_source);
	}
	if (target->indirection == NULL ||
	    index - 1U >= target->n_indirection) {
		return 0;
	}
	node = target->indirection[index - 1U];
	if (node == NULL ||
	    node->node_case != PG_QUERY__NODE__NODE_STRING ||
	    node->string == NULL) {
		return 0;
	}
	return sqlparser_current_identifier_source(
		build->handle,
		(const char *const *)&node->string->sval,
		target->location,
		index,
		out_source);
}

static int sqlparser_graph_relation_matches_res_target_path(
	sqlparser_graph_build_t *build,
	size_t relation_index,
	const PgQuery__ResTarget *target,
	size_t qualifier_count,
	int *out_used_alias)
{
	sqlparser_graph_relation_identifier_t *identifiers;
	sqlparser_graph_relation_t *relation;
	sqlparser_identifier_source_t reference_source;
	sqlparser_identifier_source_t relation_source;
	const char *reference_part;
	const char *relation_part;
	size_t reverse_index;

	if (out_used_alias != NULL) {
		*out_used_alias = 0;
	}
	relation = sqlparser_graph_relation_by_local(build, relation_index);
	if (relation == NULL || target == NULL || qualifier_count == 0U) {
		return 0;
	}
	identifiers = sqlparser_graph_relation_identifier_by_local(
		build,
		relation_index);
	if (relation->alias_name != NULL && relation->alias_name[0] != '\0') {
		memset(&relation_source, 0, sizeof(relation_source));
		if (identifiers != NULL) {
			relation_source = identifiers->alias;
		}
		reference_part = sqlparser_graph_res_target_path_part(
			target,
			0U);
		(void)sqlparser_graph_res_target_path_source(
			build,
			target,
			0U,
			&reference_source);
		if (qualifier_count != 1U ||
		    !sqlparser_identifier_semantic_equal(
			    build->handle,
			    relation->alias_name,
			    relation_source,
			    reference_part,
			    reference_source)) {
			return 0;
		}
		if (out_used_alias != NULL) {
			*out_used_alias = 1;
		}
		return 1;
	}
	for (reverse_index = 0U;
	     reverse_index < qualifier_count;
	     reverse_index++) {
		size_t reference_index;

		reference_index = qualifier_count - reverse_index - 1U;
		reference_part = sqlparser_graph_res_target_path_part(
			target,
			reference_index);
		(void)sqlparser_graph_res_target_path_source(
			build,
			target,
			reference_index,
			&reference_source);
		if (!sqlparser_graph_relation_path_part(
			    build,
			    relation_index,
			    reverse_index,
			    &relation_part,
			    &relation_source) ||
		    !sqlparser_identifier_semantic_equal(
			    build->handle,
			    relation_part,
			    relation_source,
			    reference_part,
			    reference_source)) {
			return 0;
		}
	}
	return 1;
}

static void sqlparser_graph_resolve_assignment_relation(
	sqlparser_graph_build_t *build,
	size_t block_index,
	const PgQuery__ResTarget *target,
	size_t fallback_relation_index,
	int has_fallback_relation,
	size_t *out_relation_index,
	int *out_has_relation)
{
	size_t qualifier_count;
	size_t relation_index;
	size_t match_count;
	size_t matched_relation_index;

	if (out_relation_index != NULL) {
		*out_relation_index = 0U;
	}
	if (out_has_relation != NULL) {
		*out_has_relation = 0;
	}
	if (build == NULL || target == NULL || out_relation_index == NULL ||
	    out_has_relation == NULL) {
		return;
	}
	if (target->n_indirection == 0U) {
		*out_relation_index = fallback_relation_index;
		*out_has_relation = has_fallback_relation;
		return;
	}
	if (target->n_indirection > 3U || target->indirection == NULL) {
		return;
	}
	for (qualifier_count = 0U;
	     qualifier_count <= target->n_indirection;
	     qualifier_count++) {
		if (sqlparser_graph_res_target_path_part(
			    target,
			    qualifier_count) == NULL) {
			*out_relation_index = fallback_relation_index;
			*out_has_relation = has_fallback_relation;
			return;
		}
	}
	qualifier_count = target->n_indirection;
	match_count = 0U;
	matched_relation_index = 0U;
	for (relation_index = 0U;
	     relation_index < sqlparser_graph_local_relation_count(build);
	     relation_index++) {
		sqlparser_graph_relation_t *relation;

		relation = sqlparser_graph_relation_by_local(
			build,
			relation_index);
		if (relation == NULL || relation->block_index != block_index ||
		    !sqlparser_graph_relation_matches_res_target_path(
			    build,
			    relation_index,
			    target,
			    qualifier_count,
			    NULL)) {
			continue;
		}
		matched_relation_index = relation_index;
		match_count++;
	}
	if (match_count == 1U) {
		*out_relation_index = matched_relation_index;
		*out_has_relation = 1;
	}
}

static int sqlparser_graph_collect_assignment_target(
	sqlparser_graph_build_t *build,
	size_t relation_index,
	PgQuery__ResTarget *target,
	sqlparser_error_t *out_error)
{
	sqlparser_graph_relation_t *relation;
	size_t qualifier_count;
	size_t path_index;
	int used_alias;

	if (build == NULL || !build->collect_relation_bindings ||
	    !build->collect_has_target_relation ||
	    build->collect_target_relation_ambiguous ||
	    relation_index != build->collect_target_relation_index ||
	    target == NULL || target->name == NULL ||
	    target->name[0] == '\0') {
		return 0;
	}
	if (build->handle != NULL &&
	    (build->handle->dialect == SQLPARSER_DIALECT_POSTGRESQL ||
	     build->handle->dialect ==
		     SQLPARSER_DIALECT_VASTBASE_POSTGRESQL)) {
		return 0;
	}
	if (target->n_indirection == 0U) {
		relation = sqlparser_graph_relation_by_local(
			build,
			relation_index);
		if (relation == NULL ||
		    (relation->alias_name != NULL &&
		     relation->alias_name[0] != '\0')) {
			return 0;
		}
		if (build->handle != NULL &&
		    sqlparser_dialect_is_mysql_compatible(
			    build->handle->dialect) &&
		    sqlparser_mysql_statement_has_dml_join(
			    build->handle->dialect_state,
			    build->statement_index)) {
			return sqlparser_graph_collect_append_assignment_target(
				build,
				target,
				out_error);
		}
		return 0;
	}
	if (target->indirection == NULL) {
		return 0;
	}
	for (path_index = 0U;
	     path_index <= target->n_indirection;
	     path_index++) {
		if (sqlparser_graph_res_target_path_part(
			    target,
			    path_index) == NULL) {
			return 0;
		}
	}
	qualifier_count = target->n_indirection;
	used_alias = 0;
	if (!sqlparser_graph_relation_matches_res_target_path(
		    build,
		    relation_index,
		    target,
		    qualifier_count,
		    &used_alias) ||
	    used_alias) {
		return 0;
	}
	return sqlparser_graph_collect_append_assignment_target(
		build,
		target,
		out_error);
}

static void sqlparser_graph_push_scope(
	sqlparser_graph_build_t *build,
	sqlparser_graph_scope_t *scope,
	size_t block_index,
	PgQuery__WithClause *with_clause)
{
	if (build == NULL || scope == NULL) {
		return;
	}
	scope->parent = build->scope;
	scope->block_index = block_index;
	scope->with_clause = with_clause;
	build->scope = scope;
}

static void sqlparser_graph_pop_scope(sqlparser_graph_build_t *build)
{
	if (build != NULL && build->scope != NULL) {
		build->scope = build->scope->parent;
	}
}

static PgQuery__CommonTableExpr *sqlparser_graph_find_cte(
	sqlparser_graph_build_t *build,
	const PgQuery__RangeVar *range_var,
	const char *name)
{
	sqlparser_graph_relation_identifier_t identifiers;
	sqlparser_graph_scope_t *scope;

	if (build == NULL || range_var == NULL ||
	    (range_var->catalogname != NULL && range_var->catalogname[0] != '\0') ||
	    (range_var->schemaname != NULL && range_var->schemaname[0] != '\0') ||
	    name == NULL || name[0] == '\0') {
		return NULL;
	}
	sqlparser_range_var_identifier_sources(
		build->handle,
		range_var,
		&identifiers);
	for (scope = build->scope; scope != NULL; scope = scope->parent) {
		PgQuery__CommonTableExpr *cte;

		cte = sqlparser_view_find_cte(
			build->handle,
			scope->with_clause,
			name,
			identifiers.object);
		if (cte != NULL) {
			return cte;
		}
	}
	return NULL;
}

static size_t sqlparser_graph_find_relation_selector_index(
	sqlparser_graph_build_t *build,
	PgQuery__RangeVar *range_var)
{
	size_t count;
	size_t index;
	ProtobufCMessage *message;
	sqlparser_error_t error;

	if (build == NULL || build->handle == NULL || range_var == NULL) {
		return (size_t)-1;
	}
	if (sqlparser_graph_ensure_selector_cache(build, NULL) == 0) {
		index = sqlparser_graph_selector_cache_find(
			build->relation_indices,
			build->relation_index_count,
			range_var);
		return index;
	}
	memset(&error, 0, sizeof(error));
	if (sqlparser_search_statement_messages(
		    build->handle,
		    build->statement_index,
		    &pg_query__range_var__descriptor,
		    NULL,
		    0,
		    0U,
		    &count,
		    NULL,
		    &error) != SQLPARSER_STATUS_OK) {
		return (size_t)-1;
	}
	for (index = 0U; index < count; index++) {
		message = NULL;
		if (sqlparser_search_statement_messages(
			    build->handle,
			    build->statement_index,
			    &pg_query__range_var__descriptor,
			    NULL,
			    1,
			    index,
			    NULL,
			    &message,
			    &error) != SQLPARSER_STATUS_OK) {
			return (size_t)-1;
		}
		if ((PgQuery__RangeVar *)message == range_var) {
			return index;
		}
	}
	return (size_t)-1;
}

static size_t sqlparser_graph_target_path_save(const sqlparser_graph_build_t *build)
{
	return build != NULL ? build->target_path_count : 0U;
}

static void sqlparser_graph_target_path_restore(sqlparser_graph_build_t *build, size_t saved_count)
{
	if (build != NULL && saved_count <= build->target_path_count) {
		build->target_path_count = saved_count;
	}
}

static int sqlparser_graph_target_path_push(
	sqlparser_graph_build_t *build,
	const char *kind,
	const char *name,
	size_t arg_index)
{
	sqlparser_target_path_entry_t *entry;

	if (build == NULL || kind == NULL) {
		return 0;
	}
	if (build->target_path_count >= SQLPARSER_TARGET_PATH_CAPACITY) {
		return 0;
	}
	entry = &build->target_path[build->target_path_count++];
	memset(entry, 0, sizeof(*entry));
	entry->kind = kind;
	if (name != NULL && name[0] != '\0') {
		sqlparser_view_copy_public_text(entry->name, sizeof(entry->name), name, &entry->name_truncated);
		entry->has_name = entry->name[0] != '\0';
	}
	entry->arg_index = arg_index;
	return 0;
}

static int sqlparser_graph_walk_expr(
	sqlparser_graph_build_t *build,
	size_t block_index,
	sqlparser_clause_kind_t clause,
	PgQuery__Node *node,
	size_t target_index,
	int has_target,
	sqlparser_error_t *out_error);

static int sqlparser_graph_walk_expr_with_target_path(
	sqlparser_graph_build_t *build,
	size_t block_index,
	sqlparser_clause_kind_t clause,
	PgQuery__Node *node,
	size_t target_index,
	int has_target,
	const char *kind,
	const char *name,
	size_t arg_index,
	sqlparser_error_t *out_error)
{
	size_t saved_count;
	int rc;

	saved_count = sqlparser_graph_target_path_save(build);
	if (has_target) {
		(void)sqlparser_graph_target_path_push(build, kind, name, arg_index);
	}
	rc = sqlparser_graph_walk_expr(build, block_index, clause, node, target_index, has_target, out_error);
	sqlparser_graph_target_path_restore(build, saved_count);
	return rc;
}

static int sqlparser_graph_add_column_ref_field(
	sqlparser_graph_build_t *build,
	size_t block_index,
	sqlparser_clause_kind_t clause,
	PgQuery__ColumnRef *column_ref,
	size_t target_index,
	int has_target,
	size_t *out_field_index,
	sqlparser_error_t *out_error)
{
	sqlparser_graph_field_t field;
	sqlparser_identifier_source_t column_source;
	const char *column_name;
	const char *qualifier;
	size_t name_index;
	size_t field_index;
	unsigned int target_reference_kinds;
	int source_reference;

	if (out_field_index != NULL) {
		*out_field_index = 0U;
	}
	column_name = sqlparser_graph_column_ref_part(column_ref, 0U);
	qualifier = sqlparser_graph_column_ref_part(column_ref, 1U);
	if (build != NULL && build->building_dml_result &&
	    build->dml_result_action_marker != NULL &&
	    sqlparser_text_equal_ci(column_name, build->dml_result_action_marker)) {
		return 0;
	}
	if (!sqlparser_graph_column_ref_is_recordable_field(build, column_ref)) {
		if (build != NULL && build->collect_relation_bindings &&
		    qualifier != NULL && qualifier[0] != '\0') {
			return sqlparser_graph_resolve_relation(
				build,
				block_index,
				qualifier,
				column_ref,
				NULL,
				NULL,
				NULL,
				out_error);
		}
		return 0;
	}
	target_reference_kinds =
		SQLPARSER_DIALECT_DML_TARGET_REFERENCE_NONE;
	source_reference = 0;
	memset(&field, 0, sizeof(field));
	field.block_index = block_index;
	field.clause = clause;
	field.column_name = column_name;
	column_source = sqlparser_graph_column_ref_part_source(
		build->handle,
		column_ref,
		0U);
	field.quoted_identifier =
		column_source.known && column_source.delimited;
	field.target_index = target_index;
	field.has_target = has_target;
	field.pseudo = sqlparser_graph_column_ref_is_hierarchy_pseudo(
		build,
		column_ref);
	field.prior = build != NULL && build->hierarchy_prior_depth > 0U;
	if (has_target && build != NULL && build->target_path_count > 0U) {
		field.target_path_count = build->target_path_count;
		if (field.target_path_count > SQLPARSER_TARGET_PATH_CAPACITY) {
			field.target_path_count = SQLPARSER_TARGET_PATH_CAPACITY;
		}
		memcpy(field.target_path, build->target_path, field.target_path_count * sizeof(field.target_path[0]));
	}
	if (!field.pseudo && build != NULL && build->building_dml_result &&
	    build->handle != NULL &&
	    sqlparser_dialect_is_sqlserver_compatible(
		    build->handle->dialect) &&
	    qualifier != NULL &&
	    (sqlparser_text_equal_ci(qualifier, "inserted") ||
	     sqlparser_text_equal_ci(qualifier, "deleted"))) {
		field.relation_index = build->dml_result_target_relation_index;
		field.has_relation = build->dml_result_has_target_relation;
		if (field.has_relation) {
			target_reference_kinds = sqlparser_text_equal_ci(
				qualifier, "inserted") ?
				SQLPARSER_DIALECT_DML_TARGET_REFERENCE_AFTER :
				SQLPARSER_DIALECT_DML_TARGET_REFERENCE_BEFORE;
		}
	} else if (!field.pseudo) {
		size_t resolution_block;
		int resolved_in_result_scope;

		resolution_block = build != NULL && build->building_dml_result ?
			build->dml_result_scope_block_index : block_index;
		resolved_in_result_scope = 0;
		if (build != NULL && build->building_dml_result &&
		    qualifier != NULL && qualifier[0] != '\0') {
			size_t qualifier_count;

			qualifier_count = 0U;
			if (sqlparser_graph_column_ref_qualifier_count(
				    column_ref,
				    &qualifier_count)) {
				size_t match_count;
				int used_alias;

					if (sqlparser_graph_match_relation_path(
						    build,
						    resolution_block,
						    column_ref,
						    qualifier_count,
						    &match_count,
						    &field.relation_index,
						    &used_alias,
						    &field.candidate_relations,
						    out_error) != 0) {
						return -1;
					}
					if (match_count > 0U) {
						resolved_in_result_scope = 1;
					}
					if (match_count == 1U) {
						field.has_relation = 1;
						if (sqlparser_graph_collect_resolved_column_ref(
						    build,
						    column_ref,
						    match_count,
						    field.relation_index,
						    used_alias,
						    out_error) != 0) {
						return -1;
					}
				}
			} else {
				size_t relation_index;

				for (relation_index = 0U;
				     relation_index <
					     sqlparser_graph_local_relation_count(build);
				     relation_index++) {
					sqlparser_graph_relation_t *relation;

					relation = sqlparser_graph_relation_by_local(build, relation_index);
					if (relation != NULL &&
					    relation->block_index == resolution_block &&
					    sqlparser_graph_relation_matches(
						    build,
						    relation_index,
						    relation,
						    qualifier,
						    column_ref)) {
					field.relation_index = relation_index;
					field.has_relation = 1;
					resolved_in_result_scope = 1;
						break;
					}
				}
			}
		}
		if (!resolved_in_result_scope && sqlparser_graph_resolve_relation(
			    build,
			    resolution_block,
			    qualifier,
			    column_ref,
			    &field.relation_index,
			    &field.has_relation,
			    &field.candidate_relations,
			    out_error) != 0) {
			return -1;
		}
		if (build != NULL && build->building_dml_result &&
		    field.has_relation &&
		    build->dml_result_has_target_relation &&
		    field.relation_index ==
			    build->dml_result_target_relation_index) {
			target_reference_kinds =
				build->dml_result_target_reference_kinds;
		} else if (build != NULL && build->building_dml_result &&
			   qualifier != NULL && field.has_relation) {
			source_reference = 1;
		}
	}
	name_index = sqlparser_graph_find_cached_name_index(build, sqlparser_graph_column_ref_name_slot(column_ref));
	if (name_index != (size_t)-1) {
		field.selector.kind = SQLPARSER_SELECTOR_KIND_NAME;
		field.selector.statement_index = build->statement_index;
		field.selector.item_index = name_index;
		field.has_selector = 1;
	}
	if (field.has_selector) {
		size_t index;

		for (index = 0U; index < build->cache->field_count; index++) {
			sqlparser_graph_field_t *existing;

			existing = &build->cache->fields[index];
			if (existing->statement_index == build->statement_index &&
			    existing->has_selector &&
			    sqlparser_graph_selector_equal(&existing->selector, &field.selector)) {
				if (out_field_index != NULL) {
					*out_field_index = existing->index;
				}
				if (sqlparser_graph_dml_result_add_resolved_references(
					    build,
					    target_index,
					    existing->index,
					    1,
					    field.relation_index,
					    target_reference_kinds,
					    source_reference,
					    out_error) != 0) {
					return -1;
				}
				return 0;
			}
		}
	}
	field_index = 0U;
	if (sqlparser_graph_add_field(build, &field, &field_index, out_error) != 0) {
		return -1;
	}
	if (out_field_index != NULL) {
		*out_field_index = field_index;
	}
	if (sqlparser_graph_dml_result_add_resolved_references(
		    build,
		    target_index,
		    field_index,
		    1,
		    field.relation_index,
		    target_reference_kinds,
		    source_reference,
		    out_error) != 0) {
		return -1;
	}
	return 0;
}

static int sqlparser_graph_add_column_ref_field_with_prior(
	sqlparser_graph_build_t *build,
	size_t block_index,
	sqlparser_clause_kind_t clause,
	PgQuery__ColumnRef *column_ref,
	size_t target_index,
	int has_target,
	int prior,
	size_t *out_field_index,
	sqlparser_error_t *out_error)
{
	unsigned int saved_depth;
	int status;

	saved_depth = build != NULL ? build->hierarchy_prior_depth : 0U;
	if (build != NULL && prior) {
		build->hierarchy_prior_depth++;
	}
	status = sqlparser_graph_add_column_ref_field(
		build,
		block_index,
		clause,
		column_ref,
		target_index,
		has_target,
		out_field_index,
		out_error);
	if (build != NULL) {
		build->hierarchy_prior_depth = saved_depth;
	}
	return status;
}

static const char *sqlparser_graph_like_escape_kind_name(sqlparser_graph_like_escape_kind_t kind)
{
	switch (kind) {
		case SQLPARSER_GRAPH_LIKE_ESCAPE_LITERAL:
			return "literal";
		case SQLPARSER_GRAPH_LIKE_ESCAPE_BIND:
			return "bind";
		case SQLPARSER_GRAPH_LIKE_ESCAPE_EXPRESSION:
			return "expression";
		case SQLPARSER_GRAPH_LIKE_ESCAPE_NONE:
		default:
			return "none";
	}
}

static int sqlparser_graph_fill_bind_fields(
	sqlparser_graph_build_t *build,
	PgQuery__Node *value_node,
	char *bind,
	size_t bind_size,
	int *has_bind,
	sqlparser_bind_kind_t *bind_kind,
	char *bind_sql,
	size_t bind_sql_size,
	int *has_bind_sql,
	size_t *bind_position,
	int *has_bind_position,
	int *is_system_variable,
	sqlparser_error_t *out_error)
{
	char *public_sql;
	sqlparser_view_bind_info_t bind_info;
	sqlparser_status_t status;
	int bind_status;

	if (bind != NULL && bind_size > 0U) {
		bind[0] = '\0';
	}
	if (bind_sql != NULL && bind_sql_size > 0U) {
		bind_sql[0] = '\0';
	}
	if (has_bind != NULL) {
		*has_bind = 0;
	}
	if (bind_kind != NULL) {
		*bind_kind = SQLPARSER_BIND_KIND_NONE;
	}
	if (has_bind_sql != NULL) {
		*has_bind_sql = 0;
	}
	if (bind_position != NULL) {
		*bind_position = 0U;
	}
	if (has_bind_position != NULL) {
		*has_bind_position = 0;
	}
	if (is_system_variable != NULL) {
		*is_system_variable = 0;
	}
	if (build == NULL || value_node == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "bind node is missing");
		return -1;
	}

	public_sql = NULL;
	status = sqlparser_view_render_value_node_public_sql(
		build->handle,
		build->statement_index,
		value_node,
		&public_sql,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		if (out_error != NULL && out_error->code == SQLPARSER_STATUS_OK) {
			sqlparser_error_set_message(out_error, status, "failed to render bind SQL");
		}
		return -1;
	}
	if (is_system_variable != NULL &&
	    sqlparser_view_dialect_uses_at_binds(build->handle->dialect) &&
	    public_sql[0] == '@' && public_sql[1] == '@') {
		*is_system_variable = 1;
	}
	memset(&bind_info, 0, sizeof(bind_info));
	bind_status = sqlparser_view_bind_info_from_value(
		build->handle,
		build->bind_positions,
		public_sql,
		value_node,
		&bind_info,
		out_error);
	if (bind_status < 0) {
		free(public_sql);
		if (out_error != NULL && out_error->code == SQLPARSER_STATUS_OK) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "failed to resolve bind info");
		}
		return -1;
	}
	sqlparser_view_copy_public_text(bind, bind_size, bind_status > 0 ? bind_info.name : NULL, NULL);
	sqlparser_view_copy_public_text(bind_sql, bind_sql_size, public_sql, NULL);
	if (has_bind != NULL) {
		*has_bind = bind != NULL && bind[0] != '\0';
	}
	if (bind_kind != NULL) {
		*bind_kind = bind_status > 0 ? bind_info.kind : SQLPARSER_BIND_KIND_NONE;
	}
	if (bind_position != NULL) {
		*bind_position = bind_status > 0 ? bind_info.position : 0U;
	}
	if (has_bind_position != NULL && bind_position != NULL) {
		*has_bind_position = *bind_position != 0U;
	}
	if (has_bind_sql != NULL) {
		*has_bind_sql = bind_sql != NULL && bind_sql[0] != '\0';
	}
	sqlparser_view_bind_info_release(&bind_info);
	free(public_sql);
	return 0;
}

static sqlparser_graph_operator_kind_t sqlparser_graph_operator_kind_from_name(const char *operator_name)
{
	if (operator_name == NULL) {
		return SQLPARSER_GRAPH_OPERATOR_UNKNOWN;
	}
	if (strcmp(operator_name, "LIKE") == 0) {
		return SQLPARSER_GRAPH_OPERATOR_LIKE;
	}
	if (strcmp(operator_name, "NOT LIKE") == 0) {
		return SQLPARSER_GRAPH_OPERATOR_NOT_LIKE;
	}
	if (strcmp(operator_name, "ILIKE") == 0) {
		return SQLPARSER_GRAPH_OPERATOR_ILIKE;
	}
	if (strcmp(operator_name, "NOT ILIKE") == 0) {
		return SQLPARSER_GRAPH_OPERATOR_NOT_ILIKE;
	}
	return SQLPARSER_GRAPH_OPERATOR_UNKNOWN;
}

static int sqlparser_graph_operator_is_like(const char *operator_name)
{
	return sqlparser_graph_operator_is_like_pattern(sqlparser_graph_operator_kind_from_name(operator_name));
}

static int sqlparser_graph_func_call_is_pg_like_escape(const PgQuery__FuncCall *func_call)
{
	const char *schema_name;
	const char *function_name;

	if (func_call == NULL ||
	    func_call->n_funcname < 2U ||
	    func_call->funcname == NULL ||
	    !sqlparser_node_string_value(func_call->funcname[func_call->n_funcname - 2U], &schema_name) ||
	    !sqlparser_node_string_value(func_call->funcname[func_call->n_funcname - 1U], &function_name)) {
		return 0;
	}
	return sqlparser_text_equal_ci(schema_name, "pg_catalog") &&
		sqlparser_text_equal_ci(function_name, "like_escape");
}

static int sqlparser_graph_split_like_escape(
	const PgQuery__AExpr *a_expr,
	PgQuery__Node *node,
	PgQuery__Node **out_pattern,
	PgQuery__Node **out_escape)
{
	PgQuery__FuncCall *func_call;

	if (out_pattern != NULL) {
		*out_pattern = node;
	}
	if (out_escape != NULL) {
		*out_escape = NULL;
	}
	node = sqlparser_unwrap_grouping_node(node);
	if (node == NULL ||
	    node->node_case != PG_QUERY__NODE__NODE_FUNC_CALL ||
	    node->func_call == NULL) {
		return 0;
	}
	func_call = node->func_call;
	if (!sqlparser_graph_func_call_is_pg_like_escape(func_call) ||
	    a_expr == NULL ||
	    a_expr->location < 0 ||
	    func_call->location != a_expr->location ||
	    func_call->n_args < 2U ||
	    func_call->args == NULL) {
		return 0;
	}
	if (out_pattern != NULL) {
		*out_pattern = func_call->args[0];
	}
	if (out_escape != NULL) {
		*out_escape = func_call->args[1];
	}
	return 1;
}

static int sqlparser_graph_like_escape_from_node(
	sqlparser_graph_build_t *build,
	PgQuery__Node *escape_node,
	sqlparser_graph_like_escape_t *out_escape,
	sqlparser_error_t *out_error)
{
	int is_system_variable;
	sqlparser_status_t status;

	if (out_escape == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "LIKE escape output is missing");
		return -1;
	}
	memset(out_escape, 0, sizeof(*out_escape));
	escape_node = sqlparser_unwrap_grouping_node(escape_node);
	if (escape_node == NULL) {
		return 0;
	}
	if (escape_node->node_case == PG_QUERY__NODE__NODE_A_CONST && escape_node->a_const != NULL) {
		out_escape->kind = SQLPARSER_GRAPH_LIKE_ESCAPE_LITERAL;
		status = sqlparser_fill_literal_view_from_a_const_with_sql(
			escape_node->a_const,
			build != NULL && build->handle != NULL ? sqlparser_effective_parser_sql(build->handle) : NULL,
			&out_escape->literal,
			out_error);
		return status == SQLPARSER_STATUS_OK ? 0 : -1;
	}
	if (escape_node->node_case == PG_QUERY__NODE__NODE_PARAM_REF && escape_node->param_ref != NULL) {
		out_escape->kind = SQLPARSER_GRAPH_LIKE_ESCAPE_BIND;
		is_system_variable = 0;
		if (sqlparser_graph_fill_bind_fields(
			build,
			escape_node,
			out_escape->bind,
			sizeof(out_escape->bind),
			&out_escape->has_bind,
			&out_escape->bind_kind,
			out_escape->bind_sql,
			sizeof(out_escape->bind_sql),
			&out_escape->has_bind_sql,
			&out_escape->bind_position,
			&out_escape->has_bind_position,
			&is_system_variable,
			out_error) != 0) {
			return -1;
		}
		if (is_system_variable) {
			memset(out_escape, 0, sizeof(*out_escape));
			out_escape->kind = SQLPARSER_GRAPH_LIKE_ESCAPE_EXPRESSION;
		}
		return 0;
	}
	out_escape->kind = SQLPARSER_GRAPH_LIKE_ESCAPE_EXPRESSION;
	return 0;
}

static int sqlparser_graph_value_from_node(
	sqlparser_graph_build_t *build,
	size_t block_index,
	sqlparser_clause_kind_t clause,
	const char *operator_name,
	size_t field_index,
	int has_field,
	sqlparser_graph_field_match_kind_t field_match_kind,
	PgQuery__Node *value_node,
	const sqlparser_graph_like_escape_t *like_escape,
	sqlparser_graph_value_t *out_value,
	sqlparser_error_t *out_error)
{
	int is_system_variable;
	size_t value_index;

	if (out_value == NULL) {
		return -1;
	}
	memset(out_value, 0, sizeof(*out_value));
	value_node = sqlparser_unwrap_grouping_node(value_node);
	if (value_node == NULL) {
		return 0;
	}
	out_value->block_index = block_index;
	out_value->clause = clause;
	out_value->operator_name = operator_name;
	out_value->operator_kind = sqlparser_graph_operator_kind_from_name(operator_name);
	out_value->field_index = field_index;
	out_value->has_field = has_field;
	out_value->field_match_kind = has_field ? field_match_kind : SQLPARSER_GRAPH_FIELD_MATCH_UNKNOWN;
	if (value_node->node_case == PG_QUERY__NODE__NODE_A_CONST && value_node->a_const != NULL) {
		out_value->kind = SQLPARSER_GRAPH_VALUE_LITERAL;
		(void)sqlparser_fill_literal_view_from_a_const_with_sql(
			value_node->a_const,
			build != NULL && build->handle != NULL ? sqlparser_effective_parser_sql(build->handle) : NULL,
			&out_value->literal,
			NULL);
	} else if (value_node->node_case == PG_QUERY__NODE__NODE_PARAM_REF && value_node->param_ref != NULL) {
		is_system_variable = 0;
		if (sqlparser_graph_fill_bind_fields(
			    build,
			    value_node,
			    out_value->bind,
			    sizeof(out_value->bind),
			    &out_value->has_bind,
			    &out_value->bind_kind,
			    out_value->bind_sql,
			    sizeof(out_value->bind_sql),
			    &out_value->has_bind_sql,
			    &out_value->bind_position,
			    &out_value->has_bind_position,
			    &is_system_variable,
			    out_error) != 0) {
			return -1;
		}
		if (is_system_variable) {
			return 0;
		}
		out_value->kind = SQLPARSER_GRAPH_VALUE_BIND;
	} else if (value_node->node_case == PG_QUERY__NODE__NODE_SET_TO_DEFAULT) {
		out_value->kind = SQLPARSER_GRAPH_VALUE_DEFAULT;
	} else {
		return 0;
	}
	if (like_escape != NULL && like_escape->kind != SQLPARSER_GRAPH_LIKE_ESCAPE_NONE) {
		out_value->like_escape = *like_escape;
	}
	value_index = sqlparser_graph_find_cached_value_index(build, value_node);
	if (value_index != (size_t)-1) {
		out_value->selector.kind = SQLPARSER_SELECTOR_KIND_VALUE;
		out_value->selector.statement_index = build->statement_index;
		out_value->selector.item_index = value_index;
		out_value->has_selector = 1;
	}
	return 1;
}

static int sqlparser_graph_walk_expr(
	sqlparser_graph_build_t *build,
	size_t block_index,
	sqlparser_clause_kind_t clause,
	PgQuery__Node *node,
	size_t target_index,
	int has_target,
	sqlparser_error_t *out_error);

static int sqlparser_graph_node_is_recordable_value(PgQuery__Node *node)
{
	node = sqlparser_unwrap_grouping_node(node);
	return node != NULL &&
		(node->node_case == PG_QUERY__NODE__NODE_A_CONST ||
		 node->node_case == PG_QUERY__NODE__NODE_PARAM_REF ||
		 node->node_case == PG_QUERY__NODE__NODE_SET_TO_DEFAULT);
}

static int sqlparser_graph_node_has_recordable_value(PgQuery__Node *node);

static int sqlparser_graph_node_records_as_expression_value(PgQuery__Node *node)
{
	if (node == NULL) {
		return 0;
	}
	node = sqlparser_unwrap_grouping_node(node);
	if (node == NULL) {
		return 0;
	}
	switch (node->node_case) {
		case PG_QUERY__NODE__NODE_A_EXPR:
		case PG_QUERY__NODE__NODE_TYPE_CAST:
		case PG_QUERY__NODE__NODE_COLLATE_CLAUSE:
		case PG_QUERY__NODE__NODE_FUNC_CALL:
		case PG_QUERY__NODE__NODE_COALESCE_EXPR:
		case PG_QUERY__NODE__NODE_MIN_MAX_EXPR:
		case PG_QUERY__NODE__NODE_NULL_TEST:
		case PG_QUERY__NODE__NODE_BOOLEAN_TEST:
		case PG_QUERY__NODE__NODE_CASE_EXPR:
		case PG_QUERY__NODE__NODE_CASE_WHEN:
		case PG_QUERY__NODE__NODE_ROW_EXPR:
		case PG_QUERY__NODE__NODE_A_ARRAY_EXPR:
		case PG_QUERY__NODE__NODE_ARRAY_EXPR:
			return 1;
		default:
			return 0;
	}
}

static int sqlparser_graph_node_records_single_value(PgQuery__Node *node)
{
	return sqlparser_graph_node_is_recordable_value(node) ||
		sqlparser_graph_node_records_as_expression_value(node);
}

static int sqlparser_graph_node_array_has_recordable_value(PgQuery__Node **items, size_t count)
{
	size_t index;

	if (items == NULL) {
		return 0;
	}
	for (index = 0U; index < count; index++) {
		if (sqlparser_graph_node_has_recordable_value(items[index])) {
			return 1;
		}
	}
	return 0;
}

static int sqlparser_graph_node_has_recordable_value(PgQuery__Node *node)
{
	if (sqlparser_graph_node_is_recordable_value(node)) {
		return 1;
	}
	if (node == NULL) {
		return 0;
	}
	node = sqlparser_unwrap_grouping_node(node);
	if (node == NULL) {
		return 0;
	}
	switch (node->node_case) {
		case PG_QUERY__NODE__NODE_LIST:
			return node->list != NULL ?
				sqlparser_graph_node_array_has_recordable_value(node->list->items, node->list->n_items) :
				0;
		case PG_QUERY__NODE__NODE_A_ARRAY_EXPR:
			return node->a_array_expr != NULL ?
				sqlparser_graph_node_array_has_recordable_value(
					node->a_array_expr->elements,
					node->a_array_expr->n_elements) :
				0;
		case PG_QUERY__NODE__NODE_ARRAY_EXPR:
			return node->array_expr != NULL ?
				sqlparser_graph_node_array_has_recordable_value(
					node->array_expr->elements,
					node->array_expr->n_elements) :
				0;
		case PG_QUERY__NODE__NODE_ROW_EXPR:
			return node->row_expr != NULL ?
				sqlparser_graph_node_array_has_recordable_value(
					node->row_expr->args,
					node->row_expr->n_args) :
				0;
		case PG_QUERY__NODE__NODE_A_EXPR:
			return node->a_expr != NULL &&
				(sqlparser_graph_node_has_recordable_value(node->a_expr->lexpr) ||
				 sqlparser_graph_node_has_recordable_value(node->a_expr->rexpr));
		case PG_QUERY__NODE__NODE_TYPE_CAST:
			return node->type_cast != NULL ?
				sqlparser_graph_node_has_recordable_value(node->type_cast->arg) :
				0;
		case PG_QUERY__NODE__NODE_COLLATE_CLAUSE:
			return node->collate_clause != NULL ?
				sqlparser_graph_node_has_recordable_value(node->collate_clause->arg) :
				0;
		case PG_QUERY__NODE__NODE_FUNC_CALL:
			return node->func_call != NULL ?
				sqlparser_graph_node_array_has_recordable_value(
					node->func_call->args,
					node->func_call->n_args) :
				0;
		case PG_QUERY__NODE__NODE_COALESCE_EXPR:
			return node->coalesce_expr != NULL ?
				sqlparser_graph_node_array_has_recordable_value(
					node->coalesce_expr->args,
					node->coalesce_expr->n_args) :
				0;
		case PG_QUERY__NODE__NODE_MIN_MAX_EXPR:
			return node->min_max_expr != NULL ?
				sqlparser_graph_node_array_has_recordable_value(
					node->min_max_expr->args,
					node->min_max_expr->n_args) :
				0;
		case PG_QUERY__NODE__NODE_NULL_TEST:
			return node->null_test != NULL ?
				sqlparser_graph_node_has_recordable_value(node->null_test->arg) :
				0;
		case PG_QUERY__NODE__NODE_BOOLEAN_TEST:
			return node->boolean_test != NULL ?
				sqlparser_graph_node_has_recordable_value(node->boolean_test->arg) :
				0;
		case PG_QUERY__NODE__NODE_CASE_EXPR:
			return node->case_expr != NULL &&
				(sqlparser_graph_node_has_recordable_value(node->case_expr->arg) ||
				 sqlparser_graph_node_array_has_recordable_value(node->case_expr->args, node->case_expr->n_args) ||
				 sqlparser_graph_node_has_recordable_value(node->case_expr->defresult));
		case PG_QUERY__NODE__NODE_CASE_WHEN:
			return node->case_when != NULL &&
				(sqlparser_graph_node_has_recordable_value(node->case_when->expr) ||
				 sqlparser_graph_node_has_recordable_value(node->case_when->result));
		default:
			return 0;
	}
}

static sqlparser_graph_field_match_kind_t sqlparser_graph_field_match_kind_from_expr(PgQuery__Node *node)
{
	if (node == NULL) {
		return SQLPARSER_GRAPH_FIELD_MATCH_UNKNOWN;
	}
	node = sqlparser_unwrap_grouping_node(node);
	if (node == NULL) {
		return SQLPARSER_GRAPH_FIELD_MATCH_UNKNOWN;
	}
	return node->node_case == PG_QUERY__NODE__NODE_COLUMN_REF ?
		SQLPARSER_GRAPH_FIELD_MATCH_DIRECT_FIELD :
		SQLPARSER_GRAPH_FIELD_MATCH_EXPRESSION_FIELD;
}

static int sqlparser_graph_a_expr_is_select_predicate(const PgQuery__AExpr *a_expr)
{
	const char *operator_name;

	if (a_expr == NULL) {
		return 0;
	}

	switch (a_expr->kind) {
		case PG_QUERY__A__EXPR__KIND__AEXPR_IN:
		case PG_QUERY__A__EXPR__KIND__AEXPR_LIKE:
		case PG_QUERY__A__EXPR__KIND__AEXPR_ILIKE:
		case PG_QUERY__A__EXPR__KIND__AEXPR_SIMILAR:
		case PG_QUERY__A__EXPR__KIND__AEXPR_BETWEEN:
		case PG_QUERY__A__EXPR__KIND__AEXPR_NOT_BETWEEN:
		case PG_QUERY__A__EXPR__KIND__AEXPR_BETWEEN_SYM:
		case PG_QUERY__A__EXPR__KIND__AEXPR_NOT_BETWEEN_SYM:
		case PG_QUERY__A__EXPR__KIND__AEXPR_OP_ANY:
		case PG_QUERY__A__EXPR__KIND__AEXPR_OP_ALL:
		case PG_QUERY__A__EXPR__KIND__AEXPR_DISTINCT:
		case PG_QUERY__A__EXPR__KIND__AEXPR_NOT_DISTINCT:
			return 1;
		case PG_QUERY__A__EXPR__KIND__AEXPR_OP:
			break;
		case PG_QUERY__A__EXPR__KIND__AEXPR_NULLIF:
		case PG_QUERY__A__EXPR__KIND__A_EXPR_KIND_UNDEFINED:
		default:
			return 0;
	}

	operator_name = sqlparser_a_expr_operator_name(a_expr);
	return operator_name != NULL &&
			(strcmp(operator_name, "=") == 0 ||
			 strcmp(operator_name, "<>") == 0 ||
			 strcmp(operator_name, "!=") == 0 ||
			 strcmp(operator_name, "<") == 0 ||
			 strcmp(operator_name, "<=") == 0 ||
			 strcmp(operator_name, ">") == 0 ||
			 strcmp(operator_name, ">=") == 0 ||
			 strcmp(operator_name, "!<") == 0 ||
			 strcmp(operator_name, "!>") == 0);
}

static int sqlparser_graph_prefix_a_expr_operand(
	PgQuery__Node *node,
	const char *operator_name,
	PgQuery__Node **out_operand)
{
	PgQuery__AExpr *a_expr;

	if (out_operand != NULL) {
		*out_operand = NULL;
	}
	node = sqlparser_unwrap_grouping_node(node);
	a_expr = node != NULL &&
		node->node_case == PG_QUERY__NODE__NODE_A_EXPR ?
		node->a_expr : NULL;
	if (a_expr == NULL ||
	    a_expr->kind != PG_QUERY__A__EXPR__KIND__AEXPR_OP ||
	    !sqlparser_text_equal_ci(
		    sqlparser_a_expr_operator_name(a_expr),
		    operator_name) ||
	    ((a_expr->lexpr == NULL) == (a_expr->rexpr == NULL))) {
		return 0;
	}
	if (out_operand != NULL) {
		*out_operand = a_expr->lexpr != NULL ?
			a_expr->lexpr : a_expr->rexpr;
	}
	return 1;
}

static int sqlparser_graph_clause_records_field_values(
	sqlparser_clause_kind_t clause,
	const PgQuery__AExpr *a_expr)
{
	if (clause != SQLPARSER_CLAUSE_KIND_SELECT_LIST &&
	    clause != SQLPARSER_CLAUSE_KIND_WHERE &&
	    clause != SQLPARSER_CLAUSE_KIND_START_WITH &&
	    clause != SQLPARSER_CLAUSE_KIND_CONNECT_BY &&
	    clause != SQLPARSER_CLAUSE_KIND_ON &&
	    clause != SQLPARSER_CLAUSE_KIND_HAVING &&
	    clause != SQLPARSER_CLAUSE_KIND_CONDITION) {
		return 0;
	}
	return sqlparser_graph_a_expr_is_select_predicate(a_expr);
}

static int sqlparser_graph_record_value_node(
	sqlparser_graph_build_t *build,
	size_t block_index,
	sqlparser_clause_kind_t clause,
	const char *operator_name,
	size_t field_index,
	int has_field,
	sqlparser_graph_field_match_kind_t field_match_kind,
	PgQuery__Node *node,
	const sqlparser_graph_like_escape_t *like_escape,
	size_t *out_value_index,
	sqlparser_error_t *out_error)
{
	sqlparser_graph_value_t value;
	int value_status;

	value_status = sqlparser_graph_value_from_node(
		build,
		block_index,
		clause,
		operator_name,
		field_index,
		has_field,
		field_match_kind,
		node,
		like_escape,
		&value,
		out_error);
	if (value_status < 0) {
		return -1;
	}
	if (value_status > 0 &&
	    sqlparser_graph_add_value(build, &value, out_value_index, out_error) != 0) {
		return -1;
	}
	return value_status > 0 ? 1 : 0;
}

static int sqlparser_graph_record_expression_value_node(
	sqlparser_graph_build_t *build,
	size_t block_index,
	sqlparser_clause_kind_t clause,
	const char *operator_name,
	size_t field_index,
	int has_field,
	sqlparser_graph_field_match_kind_t field_match_kind,
	const sqlparser_graph_like_escape_t *like_escape,
	size_t *out_value_index,
	sqlparser_error_t *out_error)
{
	sqlparser_graph_value_t value;

	if (!has_field) {
		return 0;
	}
	memset(&value, 0, sizeof(value));
	value.block_index = block_index;
	value.clause = clause;
	value.operator_name = operator_name;
	value.operator_kind = sqlparser_graph_operator_kind_from_name(operator_name);
	value.field_index = field_index;
	value.has_field = 1;
	value.field_match_kind = field_match_kind;
	value.kind = SQLPARSER_GRAPH_VALUE_EXPRESSION;
	if (like_escape != NULL && like_escape->kind != SQLPARSER_GRAPH_LIKE_ESCAPE_NONE) {
		value.like_escape = *like_escape;
	}
	return sqlparser_graph_add_value(build, &value, out_value_index, out_error) == 0 ? 1 : -1;
}

static int sqlparser_graph_record_value_nodes(
	sqlparser_graph_build_t *build,
	size_t block_index,
	sqlparser_clause_kind_t clause,
	const char *operator_name,
	size_t field_index,
	int has_field,
	sqlparser_graph_field_match_kind_t field_match_kind,
	PgQuery__Node *node,
	const sqlparser_graph_like_escape_t *like_escape,
	size_t *out_value_index,
	sqlparser_error_t *out_error);

static int sqlparser_graph_record_value_node_array(
	sqlparser_graph_build_t *build,
	size_t block_index,
	sqlparser_clause_kind_t clause,
	const char *operator_name,
	size_t field_index,
	int has_field,
	sqlparser_graph_field_match_kind_t field_match_kind,
	PgQuery__Node **items,
	size_t count,
	const sqlparser_graph_like_escape_t *like_escape,
	size_t *out_value_index,
	sqlparser_error_t *out_error)
{
	size_t index;
	int has_value;

	if (items == NULL) {
		return 0;
	}
	has_value = 0;
	for (index = 0U; index < count; index++) {
		int item_status;
		size_t item_value_index;

		item_value_index = 0U;
		item_status = sqlparser_graph_record_value_nodes(
			build,
			block_index,
			clause,
			operator_name,
			field_index,
			has_field,
			field_match_kind,
			items[index],
			like_escape,
			&item_value_index,
			out_error);
		if (item_status < 0) {
			return -1;
		}
		if (item_status > 0 && !has_value) {
			has_value = 1;
			if (out_value_index != NULL) {
				*out_value_index = item_value_index;
			}
		}
	}
	return has_value;
}

static int sqlparser_graph_record_value_nodes(
	sqlparser_graph_build_t *build,
	size_t block_index,
	sqlparser_clause_kind_t clause,
	const char *operator_name,
	size_t field_index,
	int has_field,
	sqlparser_graph_field_match_kind_t field_match_kind,
	PgQuery__Node *node,
	const sqlparser_graph_like_escape_t *like_escape,
	size_t *out_value_index,
	sqlparser_error_t *out_error)
{
	node = sqlparser_unwrap_grouping_node(node);
	if (node == NULL) {
		return 0;
	}
	if (has_field && sqlparser_graph_node_records_as_expression_value(node)) {
		return sqlparser_graph_record_expression_value_node(
			build,
			block_index,
			clause,
			operator_name,
			field_index,
			has_field,
			field_match_kind,
			like_escape,
			out_value_index,
			out_error);
	}
	switch (node->node_case) {
		case PG_QUERY__NODE__NODE_LIST:
			return node->list != NULL ?
				sqlparser_graph_record_value_node_array(
					build,
					block_index,
					clause,
					operator_name,
					field_index,
					has_field,
					field_match_kind,
					node->list->items,
					node->list->n_items,
					like_escape,
					out_value_index,
					out_error) :
				0;
		case PG_QUERY__NODE__NODE_A_ARRAY_EXPR:
			return node->a_array_expr != NULL ?
				sqlparser_graph_record_value_node_array(
					build,
					block_index,
					clause,
					operator_name,
					field_index,
					has_field,
					field_match_kind,
					node->a_array_expr->elements,
					node->a_array_expr->n_elements,
					like_escape,
					out_value_index,
					out_error) :
				0;
		case PG_QUERY__NODE__NODE_ARRAY_EXPR:
			return node->array_expr != NULL ?
				sqlparser_graph_record_value_node_array(
					build,
					block_index,
					clause,
					operator_name,
					field_index,
					has_field,
					field_match_kind,
					node->array_expr->elements,
					node->array_expr->n_elements,
					like_escape,
					out_value_index,
					out_error) :
				0;
		case PG_QUERY__NODE__NODE_ROW_EXPR:
			return node->row_expr != NULL ?
				sqlparser_graph_record_value_node_array(
					build,
					block_index,
					clause,
					operator_name,
					field_index,
					has_field,
					field_match_kind,
					node->row_expr->args,
					node->row_expr->n_args,
					like_escape,
					out_value_index,
					out_error) :
				0;
		case PG_QUERY__NODE__NODE_A_EXPR:
		{
			int left_status;
			int right_status;
			size_t left_value_index;
			size_t right_value_index;

			if (node->a_expr == NULL) {
				return 0;
			}
			left_value_index = 0U;
			left_status = sqlparser_graph_record_value_nodes(
				build,
				block_index,
				clause,
				operator_name,
				field_index,
				has_field,
				field_match_kind,
				node->a_expr->lexpr,
				like_escape,
				&left_value_index,
				out_error);
			if (left_status < 0) {
				return -1;
			}
			right_value_index = 0U;
			right_status = sqlparser_graph_record_value_nodes(
				build,
				block_index,
				clause,
				operator_name,
				field_index,
				has_field,
				field_match_kind,
				node->a_expr->rexpr,
				like_escape,
				&right_value_index,
				out_error);
			if (right_status < 0) {
				return -1;
			}
			if (left_status > 0 && out_value_index != NULL) {
				*out_value_index = left_value_index;
			} else if (right_status > 0 && out_value_index != NULL) {
				*out_value_index = right_value_index;
			}
			return left_status > 0 || right_status > 0 ? 1 : 0;
		}
		case PG_QUERY__NODE__NODE_TYPE_CAST:
			return node->type_cast != NULL ?
				sqlparser_graph_record_value_nodes(
					build,
					block_index,
					clause,
					operator_name,
					field_index,
					has_field,
					field_match_kind,
					node->type_cast->arg,
					like_escape,
					out_value_index,
					out_error) :
				0;
		case PG_QUERY__NODE__NODE_COLLATE_CLAUSE:
			return node->collate_clause != NULL ?
				sqlparser_graph_record_value_nodes(
					build,
					block_index,
					clause,
					operator_name,
					field_index,
					has_field,
					field_match_kind,
					node->collate_clause->arg,
					like_escape,
					out_value_index,
					out_error) :
				0;
		case PG_QUERY__NODE__NODE_FUNC_CALL:
			return node->func_call != NULL ?
				sqlparser_graph_record_value_node_array(
					build,
					block_index,
					clause,
					operator_name,
					field_index,
					has_field,
					field_match_kind,
					node->func_call->args,
					node->func_call->n_args,
					like_escape,
					out_value_index,
					out_error) :
				0;
		case PG_QUERY__NODE__NODE_COALESCE_EXPR:
			return node->coalesce_expr != NULL ?
				sqlparser_graph_record_value_node_array(
					build,
					block_index,
					clause,
					operator_name,
					field_index,
					has_field,
					field_match_kind,
					node->coalesce_expr->args,
					node->coalesce_expr->n_args,
					like_escape,
					out_value_index,
					out_error) :
				0;
		case PG_QUERY__NODE__NODE_MIN_MAX_EXPR:
			return node->min_max_expr != NULL ?
				sqlparser_graph_record_value_node_array(
					build,
					block_index,
					clause,
					operator_name,
					field_index,
					has_field,
					field_match_kind,
					node->min_max_expr->args,
					node->min_max_expr->n_args,
					like_escape,
					out_value_index,
					out_error) :
				0;
		case PG_QUERY__NODE__NODE_NULL_TEST:
			return node->null_test != NULL ?
				sqlparser_graph_record_value_nodes(
					build,
					block_index,
					clause,
					operator_name,
					field_index,
					has_field,
					field_match_kind,
					node->null_test->arg,
					like_escape,
					out_value_index,
					out_error) :
				0;
		case PG_QUERY__NODE__NODE_BOOLEAN_TEST:
			return node->boolean_test != NULL ?
				sqlparser_graph_record_value_nodes(
					build,
					block_index,
					clause,
					operator_name,
					field_index,
					has_field,
					field_match_kind,
					node->boolean_test->arg,
					like_escape,
					out_value_index,
					out_error) :
				0;
		case PG_QUERY__NODE__NODE_CASE_EXPR:
		{
			int arg_status;
			int args_status;
			int def_status;
			size_t arg_value_index;
			size_t args_value_index;
			size_t def_value_index;

			if (node->case_expr == NULL) {
				return 0;
			}
			arg_value_index = 0U;
			arg_status = sqlparser_graph_record_value_nodes(
				build,
				block_index,
				clause,
				operator_name,
				field_index,
				has_field,
				field_match_kind,
				node->case_expr->arg,
				like_escape,
				&arg_value_index,
				out_error);
			if (arg_status < 0) {
				return -1;
			}
			args_value_index = 0U;
			args_status = sqlparser_graph_record_value_node_array(
				build,
				block_index,
				clause,
				operator_name,
				field_index,
				has_field,
				field_match_kind,
				node->case_expr->args,
				node->case_expr->n_args,
				like_escape,
				&args_value_index,
				out_error);
			if (args_status < 0) {
				return -1;
			}
			def_value_index = 0U;
			def_status = sqlparser_graph_record_value_nodes(
				build,
				block_index,
				clause,
				operator_name,
				field_index,
				has_field,
				field_match_kind,
				node->case_expr->defresult,
				like_escape,
				&def_value_index,
				out_error);
			if (def_status < 0) {
				return -1;
			}
			if (arg_status > 0 && out_value_index != NULL) {
				*out_value_index = arg_value_index;
			} else if (args_status > 0 && out_value_index != NULL) {
				*out_value_index = args_value_index;
			} else if (def_status > 0 && out_value_index != NULL) {
				*out_value_index = def_value_index;
			}
			return arg_status > 0 || args_status > 0 || def_status > 0 ? 1 : 0;
		}
		case PG_QUERY__NODE__NODE_CASE_WHEN:
		{
			int expr_status;
			int result_status;
			size_t expr_value_index;
			size_t result_value_index;

			if (node->case_when == NULL) {
				return 0;
			}
			expr_value_index = 0U;
			expr_status = sqlparser_graph_record_value_nodes(
				build,
				block_index,
				clause,
				operator_name,
				field_index,
				has_field,
				field_match_kind,
				node->case_when->expr,
				like_escape,
				&expr_value_index,
				out_error);
			if (expr_status < 0) {
				return -1;
			}
			result_value_index = 0U;
			result_status = sqlparser_graph_record_value_nodes(
				build,
				block_index,
				clause,
				operator_name,
				field_index,
				has_field,
				field_match_kind,
				node->case_when->result,
				like_escape,
				&result_value_index,
				out_error);
			if (result_status < 0) {
				return -1;
			}
			if (expr_status > 0 && out_value_index != NULL) {
				*out_value_index = expr_value_index;
			} else if (result_status > 0 && out_value_index != NULL) {
				*out_value_index = result_value_index;
			}
			return expr_status > 0 || result_status > 0 ? 1 : 0;
		}
		default:
			return sqlparser_graph_record_value_node(
				build,
				block_index,
				clause,
				operator_name,
				field_index,
				has_field,
				field_match_kind,
				node,
				like_escape,
				out_value_index,
				out_error);
	}
}

typedef struct sqlparser_graph_field_value_match {
	size_t first_field_index;
	size_t first_value_index;
	size_t field_count;
	int has_unrecordable_field;
} sqlparser_graph_field_value_match_t;

static int sqlparser_graph_record_column_ref_match(
	sqlparser_graph_build_t *build,
	size_t block_index,
	sqlparser_clause_kind_t clause,
	const char *operator_name,
	PgQuery__ColumnRef *column_ref,
	PgQuery__Node *value_node,
	size_t target_index,
	int has_target,
	sqlparser_graph_field_match_kind_t field_match_kind,
	const sqlparser_graph_like_escape_t *like_escape,
	sqlparser_graph_field_value_match_t *match,
	sqlparser_error_t *out_error)
{
	const char *column_name;
	size_t field_index;
	size_t value_index;
	int value_status;

	if (!sqlparser_graph_column_ref_is_recordable_field(build, column_ref)) {
		column_name = sqlparser_graph_column_ref_part(column_ref, 0U);
		if (match != NULL &&
		    (column_name == NULL || strcmp(column_name, "*") != 0)) {
			match->has_unrecordable_field = 1;
		}
		return 0;
	}
	field_index = 0U;
	if (sqlparser_graph_add_column_ref_field(
		    build,
		    block_index,
		    clause,
		    column_ref,
		    target_index,
		    has_target,
		    &field_index,
		    out_error) != 0) {
		return -1;
	}
	value_index = 0U;
	value_status = sqlparser_graph_record_value_nodes(
		build,
		block_index,
		clause,
		operator_name,
		field_index,
		1,
		field_match_kind,
		value_node,
		like_escape,
		&value_index,
		out_error);
	if (value_status > 0 && match != NULL) {
		if (match->field_count == 0U) {
			match->first_field_index = field_index;
			match->first_value_index = value_index;
		}
		match->field_count++;
	}
	return value_status;
}

static int sqlparser_graph_record_column_ref_matches_in_array(
	sqlparser_graph_build_t *build,
	size_t block_index,
	sqlparser_clause_kind_t clause,
	const char *operator_name,
	PgQuery__Node **items,
	size_t count,
	PgQuery__Node *value_node,
	size_t target_index,
	int has_target,
	sqlparser_graph_field_match_kind_t field_match_kind,
	const sqlparser_graph_like_escape_t *like_escape,
	sqlparser_graph_field_value_match_t *match,
	sqlparser_error_t *out_error);

static int sqlparser_graph_record_column_ref_matches(
	sqlparser_graph_build_t *build,
	size_t block_index,
	sqlparser_clause_kind_t clause,
	const char *operator_name,
	PgQuery__Node *node,
	PgQuery__Node *value_node,
	size_t target_index,
	int has_target,
	sqlparser_graph_field_match_kind_t field_match_kind,
	const sqlparser_graph_like_escape_t *like_escape,
	sqlparser_graph_field_value_match_t *match,
	sqlparser_error_t *out_error)
{
	int left_status;
	int right_status;

	node = sqlparser_unwrap_grouping_node(node);
	if (node == NULL) {
		return 0;
	}
	switch (node->node_case) {
		case PG_QUERY__NODE__NODE_COLUMN_REF:
			return sqlparser_graph_record_column_ref_match(
				build,
				block_index,
				clause,
				operator_name,
				node->column_ref,
				value_node,
				target_index,
				has_target,
				field_match_kind,
				like_escape,
				match,
				out_error);
		case PG_QUERY__NODE__NODE_A_EXPR:
		{
			PgQuery__Node *prefix_operand;
			unsigned int saved_prior_depth;
			int status;

			if (node->a_expr == NULL) {
				return 0;
			}
			prefix_operand = NULL;
			if (sqlparser_graph_prefix_a_expr_operand(
				    node,
				    "PRIOR",
				    &prefix_operand)) {
				saved_prior_depth = build->hierarchy_prior_depth;
				build->hierarchy_prior_depth++;
				status = sqlparser_graph_record_column_ref_matches(
					build,
					block_index,
					clause,
					operator_name,
					prefix_operand,
					value_node,
					target_index,
					has_target,
					field_match_kind,
					like_escape,
					match,
					out_error);
				build->hierarchy_prior_depth = saved_prior_depth;
				return status;
			}
			left_status = sqlparser_graph_record_column_ref_matches(
				build,
				block_index,
				clause,
				operator_name,
				node->a_expr->lexpr,
				value_node,
				target_index,
				has_target,
				field_match_kind,
				like_escape,
				match,
				out_error);
			if (left_status < 0) {
				return -1;
			}
			right_status = sqlparser_graph_record_column_ref_matches(
				build,
				block_index,
				clause,
				operator_name,
				node->a_expr->rexpr,
				value_node,
				target_index,
				has_target,
				field_match_kind,
				like_escape,
				match,
				out_error);
			return right_status < 0 ? -1 : (left_status > 0 || right_status > 0 ? 1 : 0);
		}
		case PG_QUERY__NODE__NODE_BOOL_EXPR:
			return node->bool_expr != NULL ?
				sqlparser_graph_record_column_ref_matches_in_array(
					build,
					block_index,
					clause,
					operator_name,
					node->bool_expr->args,
					node->bool_expr->n_args,
					value_node,
					target_index,
					has_target,
					field_match_kind,
					like_escape,
					match,
					out_error) :
				0;
		case PG_QUERY__NODE__NODE_FUNC_CALL:
		{
			int matched;
			size_t index;

			if (node->func_call == NULL || node->func_call->args == NULL) {
				return 0;
			}
			matched = 0;
			for (index = 0U; index < node->func_call->n_args; index++) {
				int item_status;

				if (sqlparser_graph_func_arg_is_non_field(
					    build, node->func_call, index)) {
					continue;
				}
				item_status = sqlparser_graph_record_column_ref_matches(
					build,
					block_index,
					clause,
					operator_name,
					node->func_call->args[index],
					value_node,
					target_index,
					has_target,
					field_match_kind,
					like_escape,
					match,
					out_error);
				if (item_status < 0) {
					return -1;
				}
				if (item_status > 0) {
					matched = 1;
				}
			}
			return matched;
		}
		case PG_QUERY__NODE__NODE_TYPE_CAST:
			return node->type_cast != NULL ?
				sqlparser_graph_record_column_ref_matches(
					build,
					block_index,
					clause,
					operator_name,
					node->type_cast->arg,
					value_node,
					target_index,
					has_target,
					field_match_kind,
					like_escape,
					match,
					out_error) :
				0;
		case PG_QUERY__NODE__NODE_COLLATE_CLAUSE:
			return node->collate_clause != NULL ?
				sqlparser_graph_record_column_ref_matches(
					build,
					block_index,
					clause,
					operator_name,
					node->collate_clause->arg,
					value_node,
					target_index,
					has_target,
					field_match_kind,
					like_escape,
					match,
					out_error) :
				0;
		case PG_QUERY__NODE__NODE_A_ARRAY_EXPR:
			return node->a_array_expr != NULL ?
				sqlparser_graph_record_column_ref_matches_in_array(
					build,
					block_index,
					clause,
					operator_name,
					node->a_array_expr->elements,
					node->a_array_expr->n_elements,
					value_node,
					target_index,
					has_target,
					field_match_kind,
					like_escape,
					match,
					out_error) :
				0;
		case PG_QUERY__NODE__NODE_ARRAY_EXPR:
			return node->array_expr != NULL ?
				sqlparser_graph_record_column_ref_matches_in_array(
					build,
					block_index,
					clause,
					operator_name,
					node->array_expr->elements,
					node->array_expr->n_elements,
					value_node,
					target_index,
					has_target,
					field_match_kind,
					like_escape,
					match,
					out_error) :
				0;
		case PG_QUERY__NODE__NODE_COALESCE_EXPR:
			return node->coalesce_expr != NULL ?
				sqlparser_graph_record_column_ref_matches_in_array(
					build,
					block_index,
					clause,
					operator_name,
					node->coalesce_expr->args,
					node->coalesce_expr->n_args,
					value_node,
					target_index,
					has_target,
					field_match_kind,
					like_escape,
					match,
					out_error) :
				0;
		case PG_QUERY__NODE__NODE_MIN_MAX_EXPR:
			return node->min_max_expr != NULL ?
				sqlparser_graph_record_column_ref_matches_in_array(
					build,
					block_index,
					clause,
					operator_name,
					node->min_max_expr->args,
					node->min_max_expr->n_args,
					value_node,
					target_index,
					has_target,
					field_match_kind,
					like_escape,
					match,
					out_error) :
				0;
		case PG_QUERY__NODE__NODE_NULL_TEST:
			return node->null_test != NULL ?
				sqlparser_graph_record_column_ref_matches(
					build,
					block_index,
					clause,
					operator_name,
					node->null_test->arg,
					value_node,
					target_index,
					has_target,
					field_match_kind,
					like_escape,
					match,
					out_error) :
				0;
		case PG_QUERY__NODE__NODE_BOOLEAN_TEST:
			return node->boolean_test != NULL ?
				sqlparser_graph_record_column_ref_matches(
					build,
					block_index,
					clause,
					operator_name,
					node->boolean_test->arg,
					value_node,
					target_index,
					has_target,
					field_match_kind,
					like_escape,
					match,
					out_error) :
				0;
		case PG_QUERY__NODE__NODE_CASE_EXPR:
			if (node->case_expr == NULL) {
				return 0;
			}
			left_status = sqlparser_graph_record_column_ref_matches(
				build,
				block_index,
				clause,
				operator_name,
				node->case_expr->arg,
				value_node,
				target_index,
				has_target,
				field_match_kind,
				like_escape,
				match,
				out_error);
			if (left_status < 0) {
				return -1;
			}
			right_status = sqlparser_graph_record_column_ref_matches_in_array(
				build,
				block_index,
				clause,
				operator_name,
				node->case_expr->args,
				node->case_expr->n_args,
				value_node,
				target_index,
				has_target,
				field_match_kind,
				like_escape,
				match,
				out_error);
			if (right_status < 0) {
				return -1;
			}
			left_status = left_status > 0 || right_status > 0 ? 1 : 0;
			right_status = sqlparser_graph_record_column_ref_matches(
				build,
				block_index,
				clause,
				operator_name,
				node->case_expr->defresult,
				value_node,
				target_index,
				has_target,
				field_match_kind,
				like_escape,
				match,
				out_error);
			return right_status < 0 ? -1 : (left_status > 0 || right_status > 0 ? 1 : 0);
		case PG_QUERY__NODE__NODE_CASE_WHEN:
			if (node->case_when == NULL) {
				return 0;
			}
			return sqlparser_graph_record_column_ref_matches(
				build,
				block_index,
				clause,
				operator_name,
					node->case_when->result,
					value_node,
					target_index,
					has_target,
					field_match_kind,
					like_escape,
					match,
					out_error);
		case PG_QUERY__NODE__NODE_ROW_EXPR:
			return node->row_expr != NULL ?
				sqlparser_graph_record_column_ref_matches_in_array(
					build,
					block_index,
					clause,
					operator_name,
					node->row_expr->args,
					node->row_expr->n_args,
					value_node,
					target_index,
					has_target,
					field_match_kind,
					like_escape,
					match,
					out_error) :
				0;
		case PG_QUERY__NODE__NODE_SORT_BY:
			return node->sort_by != NULL ?
				sqlparser_graph_record_column_ref_matches(
					build,
					block_index,
					clause,
					operator_name,
					node->sort_by->node,
					value_node,
					target_index,
					has_target,
					field_match_kind,
					like_escape,
					match,
					out_error) :
				0;
		case PG_QUERY__NODE__NODE_LIST:
			return node->list != NULL ?
				sqlparser_graph_record_column_ref_matches_in_array(
					build,
					block_index,
					clause,
					operator_name,
					node->list->items,
					node->list->n_items,
					value_node,
					target_index,
					has_target,
					field_match_kind,
					like_escape,
					match,
					out_error) :
				0;
		default:
			return 0;
	}
}

static int sqlparser_graph_record_column_ref_matches_in_array(
	sqlparser_graph_build_t *build,
	size_t block_index,
	sqlparser_clause_kind_t clause,
	const char *operator_name,
	PgQuery__Node **items,
	size_t count,
	PgQuery__Node *value_node,
	size_t target_index,
	int has_target,
	sqlparser_graph_field_match_kind_t field_match_kind,
	const sqlparser_graph_like_escape_t *like_escape,
	sqlparser_graph_field_value_match_t *match,
	sqlparser_error_t *out_error)
{
	size_t index;
	int matched;

	if (items == NULL) {
		return 0;
	}
	matched = 0;
	for (index = 0U; index < count; index++) {
		int item_status;

		item_status = sqlparser_graph_record_column_ref_matches(
			build,
			block_index,
			clause,
			operator_name,
			items[index],
			value_node,
			target_index,
			has_target,
			field_match_kind,
			like_escape,
			match,
			out_error);
		if (item_status < 0) {
			return -1;
		}
		if (item_status > 0) {
			matched = 1;
		}
	}
	return matched;
}

static int sqlparser_graph_record_predicate_field_values(
	sqlparser_graph_build_t *build,
	size_t block_index,
	sqlparser_clause_kind_t clause,
	const char *operator_name,
	PgQuery__Node *field_node,
	PgQuery__Node *value_node,
	size_t target_index,
	int has_target,
	sqlparser_graph_field_match_kind_t field_match_kind,
	const sqlparser_graph_like_escape_t *like_escape,
	sqlparser_graph_field_value_match_t *match,
	sqlparser_error_t *out_error)
{
	return sqlparser_graph_record_column_ref_matches(
		build,
		block_index,
		clause,
		operator_name,
		field_node,
		value_node,
		target_index,
		has_target,
		field_match_kind,
		like_escape,
		match,
		out_error);
}

static int sqlparser_graph_record_predicate_value(
	sqlparser_graph_build_t *build,
	size_t block_index,
	sqlparser_clause_kind_t clause,
	const char *operator_name,
	PgQuery__Node *left,
	PgQuery__Node *right,
	size_t target_index,
	int has_target,
	const PgQuery__AExpr *a_expr,
	sqlparser_error_t *out_error)
{
	sqlparser_graph_field_match_kind_t field_match_kind;
	sqlparser_graph_like_escape_t like_escape;
	PgQuery__Node *pattern_node;
	PgQuery__Node *escape_node;
	const sqlparser_graph_like_escape_t *like_escape_ptr;
	int value_status;

	pattern_node = right;
	escape_node = NULL;
	like_escape_ptr = NULL;
	if (sqlparser_graph_operator_is_like(operator_name) &&
	    sqlparser_graph_split_like_escape(a_expr, right, &pattern_node, &escape_node)) {
		if (sqlparser_graph_like_escape_from_node(build, escape_node, &like_escape, out_error) != 0) {
			return -1;
		}
		like_escape_ptr = like_escape.kind != SQLPARSER_GRAPH_LIKE_ESCAPE_NONE ? &like_escape : NULL;
	}

	if (!sqlparser_graph_clause_records_field_values(clause, a_expr) ||
	    left == NULL ||
	    pattern_node == NULL ||
	    !sqlparser_graph_node_has_recordable_value(pattern_node)) {
		return 0;
	}
	field_match_kind = sqlparser_graph_field_match_kind_from_expr(left);
	value_status = sqlparser_graph_record_predicate_field_values(
		build,
		block_index,
		clause,
		operator_name,
		left,
		pattern_node,
		target_index,
		has_target,
		field_match_kind,
		like_escape_ptr,
		NULL,
		out_error);
	return value_status;
}

static sqlparser_graph_predicate_bool_t sqlparser_graph_predicate_bool_from_pg(PgQuery__BoolExprType boolop)
{
	switch (boolop) {
		case PG_QUERY__BOOL_EXPR_TYPE__AND_EXPR:
			return SQLPARSER_GRAPH_PREDICATE_BOOL_AND;
		case PG_QUERY__BOOL_EXPR_TYPE__OR_EXPR:
			return SQLPARSER_GRAPH_PREDICATE_BOOL_OR;
		case PG_QUERY__BOOL_EXPR_TYPE__NOT_EXPR:
			return SQLPARSER_GRAPH_PREDICATE_BOOL_NOT;
		case PG_QUERY__BOOL_EXPR_TYPE__BOOL_EXPR_TYPE_UNDEFINED:
		default:
			return SQLPARSER_GRAPH_PREDICATE_BOOL_NONE;
	}
}

static int sqlparser_graph_add_predicate_field_value(
	sqlparser_graph_build_t *build,
	size_t block_index,
	sqlparser_clause_kind_t clause,
	const char *operator_name,
	PgQuery__ColumnRef *field_ref,
	int field_prior,
	PgQuery__Node *value_node,
	const sqlparser_graph_like_escape_t *like_escape,
	sqlparser_graph_predicate_t *predicate,
	sqlparser_error_t *out_error)
{
	size_t field_index;
	size_t value_index;
	int value_status;

	if (!sqlparser_graph_column_ref_is_recordable_field(build, field_ref)) {
		return 0;
	}
	field_index = 0U;
	if (sqlparser_graph_add_column_ref_field_with_prior(
		    build,
		    block_index,
		    clause,
		    field_ref,
		    0U,
		    0,
		    field_prior,
		    &field_index,
		    out_error) != 0) {
		return -1;
	}
	predicate->left_field_index = field_index;
	predicate->has_left_field = 1;
	if (!sqlparser_graph_node_records_single_value(value_node)) {
		return 0;
	}
	value_index = 0U;
	value_status = sqlparser_graph_record_value_nodes(
		build,
		block_index,
		clause,
		operator_name,
		field_index,
		1,
		SQLPARSER_GRAPH_FIELD_MATCH_DIRECT_FIELD,
		value_node,
		like_escape,
		&value_index,
		out_error);
	if (value_status < 0) {
		return -1;
	}
	if (value_status > 0) {
		predicate->value_index = value_index;
		predicate->has_value = 1;
	}
	return 0;
}

static int sqlparser_graph_add_predicate_field_to_field_value(
	sqlparser_graph_build_t *build,
	size_t block_index,
	sqlparser_clause_kind_t clause,
	const char *operator_name,
	PgQuery__ColumnRef *left_ref,
	int left_prior,
	PgQuery__ColumnRef *right_ref,
	int right_prior,
	sqlparser_graph_predicate_t *predicate,
	sqlparser_error_t *out_error)
{
	sqlparser_graph_value_t value;
	size_t left_field_index;
	size_t right_field_index;
	size_t value_index;

	if (!sqlparser_graph_column_ref_is_recordable_field(build, left_ref) ||
	    !sqlparser_graph_column_ref_is_recordable_field(build, right_ref)) {
		return 0;
	}
	left_field_index = 0U;
	right_field_index = 0U;
	if (sqlparser_graph_add_column_ref_field_with_prior(
		    build,
		    block_index,
		    clause,
		    left_ref,
		    0U,
		    0,
		    left_prior,
		    &left_field_index,
		    out_error) != 0 ||
	    sqlparser_graph_add_column_ref_field_with_prior(
		    build,
		    block_index,
		    clause,
		    right_ref,
		    0U,
		    0,
		    right_prior,
		    &right_field_index,
		    out_error) != 0) {
		return -1;
	}
	memset(&value, 0, sizeof(value));
	value.block_index = block_index;
	value.clause = clause;
	value.operator_name = operator_name;
	value.operator_kind = sqlparser_graph_operator_kind_from_name(operator_name);
	value.field_index = left_field_index;
	value.has_field = 1;
	value.source_field_index = right_field_index;
	value.has_source_field = 1;
	value.field_match_kind = SQLPARSER_GRAPH_FIELD_MATCH_DIRECT_FIELD;
	value.kind = SQLPARSER_GRAPH_VALUE_FIELD;
	if (sqlparser_graph_add_value(build, &value, &value_index, out_error) != 0) {
		return -1;
	}
	predicate->left_field_index = left_field_index;
	predicate->has_left_field = 1;
	predicate->right_field_index = right_field_index;
	predicate->has_right_field = 1;
	predicate->value_index = value_index;
	predicate->has_value = 1;
	return 0;
}

static int sqlparser_graph_build_predicate_tree(
	sqlparser_graph_build_t *build,
	size_t block_index,
	sqlparser_clause_kind_t clause,
	PgQuery__Node *node,
	size_t *out_predicate_index,
	sqlparser_error_t *out_error);

static int sqlparser_graph_build_sublink(
	sqlparser_graph_build_t *build,
	PgQuery__SubLink *sub_link,
	size_t *out_block_index,
	sqlparser_error_t *out_error);

static int sqlparser_graph_func_call_is_dml_join_wrapper(
	const sqlparser_graph_build_t *build,
	const PgQuery__FuncCall *func_call)
{
	if (build == NULL || func_call == NULL) {
		return 0;
	}
	if (func_call == build->mysql_dml_join_wrapper) {
		return 1;
	}
	return build->handle != NULL &&
		build->handle->dialect == SQLPARSER_DIALECT_DAMENG &&
		sqlparser_view_func_call_is_dameng_update_join_on(func_call) &&
		sqlparser_dameng_statement_multi_update_join_condition_owner(
			build->handle->dialect_state,
			build->statement_index,
			func_call);
}

static PgQuery__FuncCall *sqlparser_graph_find_mysql_dml_join_wrapper(
	PgQuery__Node *node)
{
	node = sqlparser_unwrap_grouping_node(node);
	if (node != NULL &&
	    node->node_case == PG_QUERY__NODE__NODE_BOOL_EXPR &&
	    node->bool_expr != NULL &&
	    node->bool_expr->boolop == PG_QUERY__BOOL_EXPR_TYPE__AND_EXPR &&
	    node->bool_expr->n_args >= 2U &&
	    node->bool_expr->args != NULL) {
		node = sqlparser_unwrap_grouping_node(node->bool_expr->args[0]);
	}
	return node != NULL &&
		node->node_case == PG_QUERY__NODE__NODE_FUNC_CALL &&
		node->func_call != NULL &&
		node->func_call->n_args == 1U &&
		node->func_call->args != NULL &&
		sqlparser_view_func_call_is_mysql_join_on(node->func_call) ?
		node->func_call : NULL;
}

static int sqlparser_graph_build_bool_predicate(
	sqlparser_graph_build_t *build,
	size_t block_index,
	sqlparser_clause_kind_t clause,
	PgQuery__BoolExpr *bool_expr,
	size_t *out_predicate_index,
	sqlparser_error_t *out_error)
{
	sqlparser_graph_predicate_t predicate;
	PgQuery__Node *child_node;
	size_t visible_arg;
	size_t visible_count;
	size_t hidden_count;
	size_t predicate_index;
	size_t index;

	if (out_predicate_index != NULL) {
		*out_predicate_index = 0U;
	}
	if (bool_expr == NULL) {
		return 0;
	}
	visible_arg = 0U;
	visible_count = 0U;
	hidden_count = 0U;
	if (clause == SQLPARSER_CLAUSE_KIND_WHERE &&
	    bool_expr->boolop == PG_QUERY__BOOL_EXPR_TYPE__AND_EXPR &&
	    bool_expr->args != NULL) {
		for (index = 0U; index < bool_expr->n_args; index++) {
			child_node = sqlparser_unwrap_grouping_node(
				bool_expr->args[index]);
			if (child_node != NULL &&
			    child_node->node_case ==
				    PG_QUERY__NODE__NODE_FUNC_CALL &&
			    sqlparser_graph_func_call_is_dml_join_wrapper(
				    build, child_node->func_call)) {
				hidden_count++;
				continue;
			}
			visible_arg = index;
			visible_count++;
		}
		if (hidden_count > 0U && visible_count == 0U) {
			return 0;
		}
		if (hidden_count > 0U && visible_count == 1U) {
			return sqlparser_graph_build_predicate_tree(
				build,
				block_index,
				clause,
				bool_expr->args[visible_arg],
				out_predicate_index,
				out_error);
		}
	}
	memset(&predicate, 0, sizeof(predicate));
	predicate.block_index = block_index;
	predicate.clause = clause;
	predicate.kind = SQLPARSER_GRAPH_PREDICATE_BOOL;
	predicate.bool_operator = sqlparser_graph_predicate_bool_from_pg(bool_expr->boolop);
	if (sqlparser_graph_add_predicate(build, &predicate, &predicate_index, out_error) != 0) {
		return -1;
	}
	for (index = 0U; index < bool_expr->n_args; index++) {
		size_t child_index;
		int child_status;

		child_node = bool_expr->args != NULL ?
			sqlparser_unwrap_grouping_node(bool_expr->args[index]) : NULL;
		if (clause == SQLPARSER_CLAUSE_KIND_WHERE &&
		    child_node != NULL &&
		    child_node->node_case == PG_QUERY__NODE__NODE_FUNC_CALL &&
		    sqlparser_graph_func_call_is_dml_join_wrapper(
			    build, child_node->func_call)) {
			continue;
		}
		child_index = 0U;
		child_status = sqlparser_graph_build_predicate_tree(
			build,
			block_index,
			clause,
			bool_expr->args != NULL ? bool_expr->args[index] : NULL,
			&child_index,
			out_error);
		if (child_status < 0) {
			return -1;
		}
		if (child_status > 0 &&
		    sqlparser_graph_span_append_index(
			    build,
			    &build->cache->predicates[build->statement->predicate_offset + predicate_index].children,
			    child_index,
			    out_error) != 0) {
			return -1;
		}
	}
	if (out_predicate_index != NULL) {
		*out_predicate_index = predicate_index;
	}
	return 1;
}

static int sqlparser_graph_clause_records_function_predicate(sqlparser_clause_kind_t clause)
{
	return clause == SQLPARSER_CLAUSE_KIND_WHERE ||
			clause == SQLPARSER_CLAUSE_KIND_START_WITH ||
			clause == SQLPARSER_CLAUSE_KIND_CONNECT_BY ||
			clause == SQLPARSER_CLAUSE_KIND_ON ||
			clause == SQLPARSER_CLAUSE_KIND_HAVING;
}

static int sqlparser_graph_direct_field_value_sides(
	sqlparser_graph_build_t *build,
	PgQuery__Node *left,
	PgQuery__Node *right,
	PgQuery__ColumnRef **out_field_ref,
	PgQuery__Node **out_value_node)
{
	if (out_field_ref != NULL) {
		*out_field_ref = NULL;
	}
	if (out_value_node != NULL) {
		*out_value_node = NULL;
	}
	if (left != NULL && left->node_case == PG_QUERY__NODE__NODE_COLUMN_REF &&
	    right != NULL && sqlparser_graph_node_has_recordable_value(right) &&
	    sqlparser_graph_column_ref_is_recordable_field(build, left->column_ref)) {
		if (out_field_ref != NULL) {
			*out_field_ref = left->column_ref;
		}
		if (out_value_node != NULL) {
			*out_value_node = right;
		}
		return 1;
	}
	if (right != NULL && right->node_case == PG_QUERY__NODE__NODE_COLUMN_REF &&
	    left != NULL && sqlparser_graph_node_has_recordable_value(left) &&
	    sqlparser_graph_column_ref_is_recordable_field(build, right->column_ref)) {
		if (out_field_ref != NULL) {
			*out_field_ref = right->column_ref;
		}
		if (out_value_node != NULL) {
			*out_value_node = left;
		}
		return 1;
	}
	return 0;
}

static int sqlparser_graph_expression_direct_value_sides(
	PgQuery__Node *left,
	PgQuery__Node *right,
	PgQuery__Node **out_expression_node,
	PgQuery__Node **out_value_node,
	int *out_expression_is_left)
{
	if (out_expression_node != NULL) {
		*out_expression_node = NULL;
	}
	if (out_value_node != NULL) {
		*out_value_node = NULL;
	}
	if (out_expression_is_left != NULL) {
		*out_expression_is_left = 0;
	}
	if (left != NULL &&
	    left->node_case != PG_QUERY__NODE__NODE_COLUMN_REF &&
	    !sqlparser_graph_node_is_recordable_value(left) &&
	    sqlparser_graph_node_is_recordable_value(right)) {
		if (out_expression_node != NULL) {
			*out_expression_node = left;
		}
		if (out_value_node != NULL) {
			*out_value_node = right;
		}
		if (out_expression_is_left != NULL) {
			*out_expression_is_left = 1;
		}
		return 1;
	}
	if (right != NULL &&
	    right->node_case != PG_QUERY__NODE__NODE_COLUMN_REF &&
	    !sqlparser_graph_node_is_recordable_value(right) &&
	    sqlparser_graph_node_is_recordable_value(left)) {
		if (out_expression_node != NULL) {
			*out_expression_node = right;
		}
		if (out_value_node != NULL) {
			*out_value_node = left;
		}
		return 1;
	}
	return 0;
}

static int sqlparser_graph_build_a_expr_predicate(
	sqlparser_graph_build_t *build,
	size_t block_index,
	sqlparser_clause_kind_t clause,
	PgQuery__AExpr *a_expr,
	size_t *out_predicate_index,
	sqlparser_error_t *out_error)
{
	sqlparser_graph_predicate_t predicate;
	sqlparser_graph_like_escape_t like_escape;
	PgQuery__Node *left;
	PgQuery__Node *right;
	PgQuery__Node *pattern_node;
	PgQuery__Node *escape_node;
	PgQuery__Node *expression_node;
	PgQuery__Node *expression_value_node;
	PgQuery__Node *prefix_operand;
	PgQuery__Node *value_node;
	PgQuery__ColumnRef *field_ref;
	const sqlparser_graph_like_escape_t *like_escape_ptr;
	const char *operator_name;
	size_t predicate_index;
	int has_direct_field_value;
	int has_expression_direct_value;
	int expression_is_left;
	int field_prior;
	int left_prior;
	int right_prior;

	if (out_predicate_index != NULL) {
		*out_predicate_index = 0U;
	}
	if (a_expr == NULL || !sqlparser_graph_clause_records_field_values(clause, a_expr)) {
		return 0;
	}
	operator_name = sqlparser_a_expr_operator_name(a_expr);
	left = sqlparser_unwrap_grouping_node(a_expr->lexpr);
	right = sqlparser_unwrap_grouping_node(a_expr->rexpr);
	prefix_operand = NULL;
	left_prior = sqlparser_graph_prefix_a_expr_operand(
		left,
		"PRIOR",
		&prefix_operand);
	if (left_prior) {
		left = sqlparser_unwrap_grouping_node(prefix_operand);
	}
	prefix_operand = NULL;
	right_prior = sqlparser_graph_prefix_a_expr_operand(
		right,
		"PRIOR",
		&prefix_operand);
	if (right_prior) {
		right = sqlparser_unwrap_grouping_node(prefix_operand);
	}
	pattern_node = right;
	escape_node = NULL;
	like_escape_ptr = NULL;
	if (sqlparser_graph_operator_is_like(operator_name) &&
	    sqlparser_graph_split_like_escape(a_expr, right, &pattern_node, &escape_node)) {
		if (sqlparser_graph_like_escape_from_node(build, escape_node, &like_escape, out_error) != 0) {
			return -1;
		}
		like_escape_ptr = like_escape.kind != SQLPARSER_GRAPH_LIKE_ESCAPE_NONE ? &like_escape : NULL;
	}
	field_ref = NULL;
	value_node = NULL;
	has_direct_field_value = sqlparser_graph_direct_field_value_sides(
		build,
		left,
		pattern_node,
		&field_ref,
		&value_node);
	field_prior = has_direct_field_value && field_ref != NULL &&
		((left != NULL &&
		  left->node_case == PG_QUERY__NODE__NODE_COLUMN_REF &&
		  field_ref == left->column_ref && left_prior) ||
		 (right != NULL &&
		  right->node_case == PG_QUERY__NODE__NODE_COLUMN_REF &&
		  field_ref == right->column_ref && right_prior));
	expression_node = NULL;
	expression_value_node = NULL;
	expression_is_left = 0;
	has_expression_direct_value =
		sqlparser_graph_clause_records_function_predicate(clause) &&
		sqlparser_graph_expression_direct_value_sides(
			left,
			pattern_node,
			&expression_node,
			&expression_value_node,
			&expression_is_left);

	memset(&predicate, 0, sizeof(predicate));
	predicate.block_index = block_index;
	predicate.clause = clause;
	predicate.kind = SQLPARSER_GRAPH_PREDICATE_COMPARISON;
	predicate.operator_name = operator_name;
	predicate.operator_kind = sqlparser_graph_operator_kind_from_name(operator_name);
	if (left != NULL && left->node_case == PG_QUERY__NODE__NODE_COLUMN_REF &&
	    pattern_node != NULL && pattern_node->node_case == PG_QUERY__NODE__NODE_COLUMN_REF &&
	    sqlparser_graph_column_ref_is_recordable_field(build, left->column_ref) &&
	    sqlparser_graph_column_ref_is_recordable_field(build, pattern_node->column_ref)) {
		if (sqlparser_graph_add_predicate_field_to_field_value(
			    build,
			    block_index,
			    clause,
			    operator_name,
			    left->column_ref,
			    left_prior,
			    pattern_node->column_ref,
			    right_prior,
			    &predicate,
			    out_error) != 0) {
			return -1;
		}
	} else if (has_direct_field_value) {
		if (sqlparser_graph_add_predicate_field_value(
			    build,
			    block_index,
			    clause,
			    operator_name,
			    field_ref,
			    field_prior,
			    value_node,
			    like_escape_ptr,
			    &predicate,
			    out_error) != 0) {
			return -1;
		}
	} else {
		predicate.kind = SQLPARSER_GRAPH_PREDICATE_EXPRESSION;
		if (left != NULL &&
		    left->node_case == PG_QUERY__NODE__NODE_COLUMN_REF &&
		    sqlparser_graph_column_ref_is_recordable_field(
			    build,
			    left->column_ref)) {
			if (sqlparser_graph_add_column_ref_field_with_prior(
				    build,
				    block_index,
				    clause,
				    left->column_ref,
				    0U,
				    0,
				    left_prior,
				    &predicate.left_field_index,
				    out_error) != 0) {
				return -1;
			}
			predicate.has_left_field = 1;
		}
		if (pattern_node != NULL &&
		    pattern_node->node_case == PG_QUERY__NODE__NODE_COLUMN_REF &&
		    sqlparser_graph_column_ref_is_recordable_field(
			    build,
			    pattern_node->column_ref)) {
			if (sqlparser_graph_add_column_ref_field_with_prior(
				    build,
				    block_index,
				    clause,
				    pattern_node->column_ref,
				    0U,
				    0,
				    right_prior,
				    &predicate.right_field_index,
				    out_error) != 0) {
				return -1;
			}
			predicate.has_right_field = 1;
		}
		if (has_expression_direct_value) {
			sqlparser_graph_field_value_match_t match;
			size_t value_index;
			int value_status;

			memset(&match, 0, sizeof(match));
			value_status = sqlparser_graph_record_predicate_field_values(
				build,
				block_index,
				clause,
				operator_name,
				expression_is_left && left_prior ?
					a_expr->lexpr :
					(!expression_is_left && right_prior ?
					 a_expr->rexpr : expression_node),
				expression_value_node,
				0U,
				0,
				SQLPARSER_GRAPH_FIELD_MATCH_EXPRESSION_FIELD,
				like_escape_ptr,
				&match,
				out_error);
			if (value_status < 0) {
				return -1;
			}
			if (value_status > 0) {
				predicate.value_index = match.first_value_index;
				predicate.has_value = 1;
				if (match.field_count == 1U) {
					if (expression_is_left) {
						predicate.left_field_index = match.first_field_index;
						predicate.has_left_field = 1;
					} else {
						predicate.right_field_index = match.first_field_index;
						predicate.has_right_field = 1;
					}
				}
			} else if (!match.has_unrecordable_field) {
				value_index = 0U;
				value_status = sqlparser_graph_record_value_node(
					build,
					block_index,
					clause,
					operator_name,
					0U,
					0,
					SQLPARSER_GRAPH_FIELD_MATCH_UNKNOWN,
					expression_value_node,
					like_escape_ptr,
					&value_index,
					out_error);
				if (value_status < 0) {
					return -1;
				}
				if (value_status > 0) {
					predicate.value_index = value_index;
					predicate.has_value = 1;
				}
			}
		} else {
			int records_expression_values;
			int left_status;
			int left_is_pseudo;
			int right_is_pseudo;
			int right_status;
			size_t first_value_index;

			first_value_index = sqlparser_graph_local_value_count(build);
			left_is_pseudo =
				left != NULL &&
				left->node_case ==
					PG_QUERY__NODE__NODE_COLUMN_REF &&
				sqlparser_graph_column_ref_is_pseudo(
					build,
					left->column_ref);
			right_is_pseudo =
				pattern_node != NULL &&
				pattern_node->node_case ==
					PG_QUERY__NODE__NODE_COLUMN_REF &&
				sqlparser_graph_column_ref_is_pseudo(
					build,
					pattern_node->column_ref);
			records_expression_values =
				clause == SQLPARSER_CLAUSE_KIND_CONDITION ||
				(left != NULL &&
				 left->node_case ==
					 PG_QUERY__NODE__NODE_COLUMN_REF &&
				 sqlparser_graph_column_ref_is_dialect_expression(
					 build,
					 left->column_ref)) ||
				(pattern_node != NULL &&
				 pattern_node->node_case ==
					 PG_QUERY__NODE__NODE_COLUMN_REF &&
				 sqlparser_graph_column_ref_is_dialect_expression(
					 build,
					 pattern_node->column_ref)) ||
				(left_is_pseudo &&
				 sqlparser_graph_node_is_recordable_value(pattern_node)) ||
				(right_is_pseudo &&
				 sqlparser_graph_node_is_recordable_value(left));
			left_status = !records_expression_values || left_is_pseudo ?
				0 :
				sqlparser_graph_record_value_nodes(
					build,
					block_index,
					clause,
					operator_name,
					0U,
					0,
					SQLPARSER_GRAPH_FIELD_MATCH_UNKNOWN,
					left,
					like_escape_ptr,
					NULL,
					out_error);
			if (left_status < 0) {
				return -1;
			}
			right_status = !records_expression_values || right_is_pseudo ?
				0 :
				sqlparser_graph_record_value_nodes(
					build,
					block_index,
					clause,
					operator_name,
					0U,
					0,
					SQLPARSER_GRAPH_FIELD_MATCH_UNKNOWN,
					pattern_node,
					like_escape_ptr,
					NULL,
					out_error);
			if (right_status < 0) {
				return -1;
			}
			if ((left_status > 0 || right_status > 0) &&
			    first_value_index < sqlparser_graph_local_value_count(build)) {
				predicate.value_index = first_value_index;
				predicate.has_value = 1;
			}
		}
	}
	if (!predicate.has_left_field && !predicate.has_right_field && !predicate.has_value) {
		return 0;
	}
	if (sqlparser_graph_add_predicate(build, &predicate, &predicate_index, out_error) != 0) {
		return -1;
	}
	if (out_predicate_index != NULL) {
		*out_predicate_index = predicate_index;
	}
	return 1;
}

static int sqlparser_graph_build_func_call_predicate(
	sqlparser_graph_build_t *build,
	size_t block_index,
	sqlparser_clause_kind_t clause,
	PgQuery__FuncCall *func_call,
	size_t *out_predicate_index,
	sqlparser_error_t *out_error)
{
	sqlparser_graph_predicate_t predicate;
	const char *operator_name;
	PgQuery__ColumnRef *field_ref;
	PgQuery__Node *value_node;
	size_t predicate_index;
	size_t index;

	if (out_predicate_index != NULL) {
		*out_predicate_index = 0U;
	}
	if (func_call == NULL ||
	    func_call->n_args == 0U ||
	    func_call->args == NULL ||
	    !sqlparser_graph_clause_records_function_predicate(clause)) {
		return 0;
	}
	operator_name = sqlparser_view_func_call_name(func_call);
	if (operator_name == NULL || operator_name[0] == '\0') {
		return 0;
	}
	field_ref = NULL;
	value_node = NULL;
	for (index = 0U; index < func_call->n_args; index++) {
		PgQuery__Node *arg;

		if (sqlparser_graph_func_arg_is_non_field(
			    build, func_call, index)) {
			continue;
		}
		arg = func_call->args[index];
		if (arg == NULL) {
			continue;
		}
		if (field_ref == NULL &&
		    arg->node_case == PG_QUERY__NODE__NODE_COLUMN_REF &&
		    sqlparser_graph_column_ref_is_recordable_field(
			    build,
			    arg->column_ref)) {
			field_ref = arg->column_ref;
			continue;
		}
		if (value_node == NULL && sqlparser_graph_node_is_recordable_value(arg)) {
			value_node = arg;
		}
	}
	if (field_ref == NULL) {
		return 0;
	}
	memset(&predicate, 0, sizeof(predicate));
	predicate.block_index = block_index;
	predicate.clause = clause;
	predicate.kind = SQLPARSER_GRAPH_PREDICATE_EXPRESSION;
	predicate.operator_name = operator_name;
	predicate.operator_kind = sqlparser_graph_operator_kind_from_name(operator_name);
	if (sqlparser_graph_add_predicate_field_value(
		    build,
		    block_index,
		    clause,
		    operator_name,
		    field_ref,
		    0,
		    value_node,
		    NULL,
		    &predicate,
		    out_error) != 0) {
		return -1;
	}
	if (!predicate.has_left_field && !predicate.has_value) {
		return 0;
	}
	if (sqlparser_graph_add_predicate(build, &predicate, &predicate_index, out_error) != 0) {
		return -1;
	}
	if (out_predicate_index != NULL) {
		*out_predicate_index = predicate_index;
	}
	return 1;
}

static int sqlparser_graph_build_null_test_predicate(
	sqlparser_graph_build_t *build,
	size_t block_index,
	sqlparser_clause_kind_t clause,
	PgQuery__NullTest *null_test,
	size_t *out_predicate_index,
	sqlparser_error_t *out_error)
{
	sqlparser_graph_predicate_t predicate;
	PgQuery__Node *arg;
	const char *operator_name;
	size_t predicate_index;
	size_t value_index;
	int value_status;

	if (out_predicate_index != NULL) {
		*out_predicate_index = 0U;
	}
	if (null_test == NULL) {
		return 0;
	}
	switch (null_test->nulltesttype) {
		case PG_QUERY__NULL_TEST_TYPE__IS_NULL:
			operator_name = "IS NULL";
			break;
		case PG_QUERY__NULL_TEST_TYPE__IS_NOT_NULL:
			operator_name = "IS NOT NULL";
			break;
		case PG_QUERY__NULL_TEST_TYPE__NULL_TEST_TYPE_UNDEFINED:
		default:
			return 0;
	}
	arg = sqlparser_unwrap_grouping_node(null_test->arg);
	if (arg == NULL) {
		return 0;
	}
	memset(&predicate, 0, sizeof(predicate));
	predicate.block_index = block_index;
	predicate.clause = clause;
	predicate.kind = SQLPARSER_GRAPH_PREDICATE_EXPRESSION;
	predicate.operator_name = operator_name;
	predicate.operator_kind = sqlparser_graph_operator_kind_from_name(operator_name);
	if (arg->node_case == PG_QUERY__NODE__NODE_COLUMN_REF) {
		if (sqlparser_graph_add_predicate_field_value(
			    build,
			    block_index,
			    clause,
			    operator_name,
			    arg->column_ref,
			    0,
			    NULL,
			    NULL,
			    &predicate,
			    out_error) != 0) {
			return -1;
		}
		if (predicate.has_left_field) {
			predicate.kind = SQLPARSER_GRAPH_PREDICATE_COMPARISON;
		}
	} else if (sqlparser_graph_node_is_recordable_value(arg)) {
		value_index = 0U;
		value_status = sqlparser_graph_record_value_node(
			build,
			block_index,
			clause,
			operator_name,
			0U,
			0,
			SQLPARSER_GRAPH_FIELD_MATCH_UNKNOWN,
			arg,
			NULL,
			&value_index,
			out_error);
		if (value_status < 0) {
			return -1;
		}
		if (value_status > 0) {
			predicate.value_index = value_index;
			predicate.has_value = 1;
		}
	}
	if (sqlparser_graph_add_predicate(build, &predicate, &predicate_index, out_error) != 0) {
		return -1;
	}
	if (out_predicate_index != NULL) {
		*out_predicate_index = predicate_index;
	}
	return 1;
}

static int sqlparser_graph_build_predicate_tree(
	sqlparser_graph_build_t *build,
	size_t block_index,
	sqlparser_clause_kind_t clause,
	PgQuery__Node *node,
	size_t *out_predicate_index,
	sqlparser_error_t *out_error)
{
	sqlparser_graph_predicate_t predicate;
	PgQuery__Node *testexpr;
	size_t predicate_index;

	if (out_predicate_index != NULL) {
		*out_predicate_index = 0U;
	}
	node = sqlparser_unwrap_grouping_node(node);
	if (node == NULL) {
		return 0;
	}
	if (node->node_case == PG_QUERY__NODE__NODE_FUNC_CALL &&
	    sqlparser_graph_func_call_is_dml_join_wrapper(
		    build, node->func_call)) {
		return 0;
	}
	switch (node->node_case) {
		case PG_QUERY__NODE__NODE_BOOL_EXPR:
			return sqlparser_graph_build_bool_predicate(
				build,
				block_index,
				clause,
				node->bool_expr,
				out_predicate_index,
				out_error);
		case PG_QUERY__NODE__NODE_A_EXPR:
			return sqlparser_graph_build_a_expr_predicate(
				build,
				block_index,
				clause,
				node->a_expr,
				out_predicate_index,
				out_error);
		case PG_QUERY__NODE__NODE_SUB_LINK:
			if (node->sub_link == NULL) {
				return 0;
			}
			memset(&predicate, 0, sizeof(predicate));
			predicate.block_index = block_index;
			predicate.clause = clause;
			if (node->sub_link->sub_link_type == PG_QUERY__SUB_LINK_TYPE__EXISTS_SUBLINK) {
				predicate.kind = SQLPARSER_GRAPH_PREDICATE_EXISTS;
			} else if (node->sub_link->sub_link_type == PG_QUERY__SUB_LINK_TYPE__ANY_SUBLINK &&
			           node->sub_link->n_oper_name == 0U &&
			           node->sub_link->subselect != NULL &&
			           node->sub_link->subselect->node_case == PG_QUERY__NODE__NODE_SELECT_STMT &&
			           node->sub_link->subselect->select_stmt != NULL) {
				testexpr = sqlparser_unwrap_grouping_node(node->sub_link->testexpr);
				if (testexpr == NULL) {
					return 0;
				}
				predicate.kind = SQLPARSER_GRAPH_PREDICATE_COMPARISON;
				predicate.operator_name = "IN";
				predicate.operator_kind = SQLPARSER_GRAPH_OPERATOR_UNKNOWN;
				if (testexpr->node_case == PG_QUERY__NODE__NODE_COLUMN_REF &&
				    sqlparser_graph_add_predicate_field_value(
					    build,
					    block_index,
					    clause,
					    predicate.operator_name,
					    testexpr->column_ref,
					    0,
					    NULL,
					    NULL,
					    &predicate,
					    out_error) != 0) {
					return -1;
				}
			} else {
				return 0;
			}
			if (sqlparser_graph_add_predicate(build, &predicate, &predicate_index, out_error) != 0) {
				return -1;
			}
			if (out_predicate_index != NULL) {
				*out_predicate_index = predicate_index;
			}
			return 1;
		case PG_QUERY__NODE__NODE_FUNC_CALL:
			if (node->func_call == NULL) {
				return 0;
			}
			return sqlparser_graph_build_func_call_predicate(
				build,
				block_index,
				clause,
				node->func_call,
				out_predicate_index,
				out_error);
		case PG_QUERY__NODE__NODE_NULL_TEST:
			return sqlparser_graph_build_null_test_predicate(
				build,
				block_index,
				clause,
				node->null_test,
				out_predicate_index,
				out_error);
		default:
			return 0;
	}
}

static int sqlparser_graph_walk_predicate_node(
	sqlparser_graph_build_t *build,
	size_t block_index,
	sqlparser_clause_kind_t clause,
	PgQuery__Node *node,
	sqlparser_error_t *out_error)
{
	size_t index;

	node = sqlparser_unwrap_grouping_node(node);
	if (node == NULL) {
		return 0;
	}
	if (node->node_case == PG_QUERY__NODE__NODE_BOOL_EXPR &&
	    node->bool_expr != NULL) {
		for (index = 0U; index < node->bool_expr->n_args; index++) {
			if (sqlparser_graph_walk_predicate_node(
				    build,
				    block_index,
				    clause,
				    node->bool_expr->args != NULL ? node->bool_expr->args[index] : NULL,
				    out_error) != 0) {
				return -1;
			}
		}
		return 0;
	}
	if (node->node_case == PG_QUERY__NODE__NODE_FUNC_CALL &&
	    node->func_call != NULL &&
	    sqlparser_graph_func_call_is_dml_join_wrapper(
		    build, node->func_call)) {
		for (index = 0U; index < node->func_call->n_args; index++) {
			if (node->func_call != build->mysql_dml_join_wrapper &&
			    sqlparser_graph_build_predicate_tree(
				    build,
				    block_index,
				    SQLPARSER_CLAUSE_KIND_ON,
				    node->func_call->args != NULL ?
					    node->func_call->args[index] : NULL,
				    NULL,
				    out_error) < 0) {
				return -1;
			}
			if (sqlparser_graph_walk_predicate_node(
				    build,
				    block_index,
				    SQLPARSER_CLAUSE_KIND_ON,
				    node->func_call->args != NULL ? node->func_call->args[index] : NULL,
				    out_error) != 0) {
				return -1;
			}
		}
		return 0;
	}
	if (node->node_case == PG_QUERY__NODE__NODE_SUB_LINK &&
	    node->sub_link != NULL &&
	    node->sub_link->sub_link_type == PG_QUERY__SUB_LINK_TYPE__ANY_SUBLINK &&
	    node->sub_link->n_oper_name == 0U &&
	    node->sub_link->subselect != NULL &&
	    node->sub_link->subselect->node_case == PG_QUERY__NODE__NODE_SELECT_STMT &&
	    node->sub_link->subselect->select_stmt != NULL) {
		PgQuery__Node *testexpr;

		testexpr = sqlparser_unwrap_grouping_node(node->sub_link->testexpr);
		if (testexpr != NULL &&
		    testexpr->node_case == PG_QUERY__NODE__NODE_COLUMN_REF) {
			return sqlparser_graph_build_sublink(
				build,
				node->sub_link,
				NULL,
				out_error);
		}
	}
	if (node->node_case == PG_QUERY__NODE__NODE_NULL_TEST &&
	    node->null_test != NULL) {
		PgQuery__Node *arg;

		arg = sqlparser_unwrap_grouping_node(node->null_test->arg);
		if ((arg != NULL && arg->node_case == PG_QUERY__NODE__NODE_COLUMN_REF) ||
		    sqlparser_graph_node_is_recordable_value(arg)) {
			return 0;
		}
		return sqlparser_graph_walk_expr(
			build,
			block_index,
			clause,
			arg,
			0U,
			0,
			out_error);
	}
	if (node->node_case == PG_QUERY__NODE__NODE_A_EXPR &&
	    node->a_expr != NULL &&
	    sqlparser_graph_clause_records_field_values(clause, node->a_expr)) {
		PgQuery__Node *left;
		PgQuery__Node *right;
		PgQuery__Node *pattern_node;
		PgQuery__Node *value_node;
		const char *operator_name;
		int builder_records_value;

		operator_name = sqlparser_a_expr_operator_name(node->a_expr);
		left = sqlparser_unwrap_grouping_node(node->a_expr->lexpr);
		right = sqlparser_unwrap_grouping_node(node->a_expr->rexpr);
		pattern_node = right;
		if (sqlparser_graph_operator_is_like(operator_name)) {
			(void)sqlparser_graph_split_like_escape(
				node->a_expr,
				right,
				&pattern_node,
				NULL);
		}
		value_node = NULL;
		builder_records_value =
			sqlparser_graph_direct_field_value_sides(
				build,
				left,
				pattern_node,
				NULL,
				&value_node) &&
			sqlparser_graph_node_records_single_value(value_node);
		if (!builder_records_value &&
		    sqlparser_graph_clause_records_function_predicate(clause)) {
			builder_records_value = sqlparser_graph_expression_direct_value_sides(
				left,
				pattern_node,
				NULL,
				NULL,
				NULL);
		}
		if (builder_records_value) {
			if (sqlparser_graph_walk_expr(
				    build,
				    block_index,
				    clause,
				    node->a_expr->lexpr,
				    0U,
				    0,
				    out_error) != 0) {
				return -1;
			}
			return sqlparser_graph_walk_expr(
				build,
				block_index,
				clause,
				node->a_expr->rexpr,
				0U,
				0,
				out_error);
		}
	}
	return sqlparser_graph_walk_expr(build, block_index, clause, node, 0U, 0, out_error);
}

static int sqlparser_graph_walk_predicate_expr(
	sqlparser_graph_build_t *build,
	size_t block_index,
	sqlparser_clause_kind_t clause,
	PgQuery__Node *node,
	sqlparser_error_t *out_error)
{
	const PgQuery__FuncCall *saved_wrapper;
	PgQuery__FuncCall *wrapper;
	int expression_status;
	int predicate_status;

	saved_wrapper = build->mysql_dml_join_wrapper;
	wrapper = NULL;
	if (saved_wrapper == NULL &&
	    clause == SQLPARSER_CLAUSE_KIND_WHERE &&
	    build->handle != NULL &&
	    sqlparser_dialect_is_mysql_compatible(build->handle->dialect) &&
	    sqlparser_mysql_statement_has_dml_join(
		    build->handle->dialect_state,
		    build->statement_index)) {
		wrapper = sqlparser_graph_find_mysql_dml_join_wrapper(node);
	}
	if (wrapper != NULL) {
		build->mysql_dml_join_wrapper = wrapper;
		if (build->collect_relation_bindings) {
			expression_status = sqlparser_graph_walk_expr(
				build,
				block_index,
				clause,
				node,
				0U,
				0,
				out_error);
			build->mysql_dml_join_wrapper = saved_wrapper;
			return expression_status;
		}
		predicate_status = sqlparser_graph_build_predicate_tree(
			build,
			block_index,
			SQLPARSER_CLAUSE_KIND_ON,
			wrapper->args[0],
			NULL,
			out_error);
		if (predicate_status >= 0) {
			predicate_status = sqlparser_graph_build_predicate_tree(
					build,
					block_index,
					clause,
					node,
					NULL,
					out_error);
		}
		expression_status = predicate_status < 0 ? -1 :
			sqlparser_graph_walk_predicate_node(
				build,
				block_index,
				clause,
				node,
				out_error);
		build->mysql_dml_join_wrapper = saved_wrapper;
		return expression_status;
	}
	if (build->collect_relation_bindings) {
		return sqlparser_graph_walk_expr(
			build,
			block_index,
			clause,
			node,
			0U,
			0,
			out_error);
	}
	predicate_status = sqlparser_graph_build_predicate_tree(build, block_index, clause, node, NULL, out_error);
	if (predicate_status < 0) {
		return -1;
	}
	return sqlparser_graph_walk_predicate_node(build, block_index, clause, node, out_error);
}

static int sqlparser_graph_walk_node_array(
	sqlparser_graph_build_t *build,
	size_t block_index,
	sqlparser_clause_kind_t clause,
	PgQuery__Node **items,
	size_t count,
	sqlparser_error_t *out_error)
{
	size_t index;

	for (index = 0U; index < count; index++) {
		if (sqlparser_graph_walk_expr(build, block_index, clause, items[index], 0U, 0, out_error) != 0) {
			return -1;
		}
	}
	return 0;
}

static int sqlparser_graph_walk_node_array_with_target_path(
	sqlparser_graph_build_t *build,
	size_t block_index,
	sqlparser_clause_kind_t clause,
	PgQuery__Node **items,
	size_t count,
	size_t target_index,
	int has_target,
	const char *kind,
	const char *name,
	sqlparser_error_t *out_error)
{
	size_t index;

	for (index = 0U; index < count; index++) {
		if (sqlparser_graph_walk_expr_with_target_path(
			    build,
			    block_index,
			    clause,
			    items[index],
			    target_index,
			    has_target,
			    kind,
			    name,
			    index,
			    out_error) != 0) {
			return -1;
		}
	}
	return 0;
}

static int sqlparser_graph_walk_window_def_with_target_path(
	sqlparser_graph_build_t *build,
	size_t block_index,
	sqlparser_clause_kind_t clause,
	PgQuery__WindowDef *window,
	size_t target_index,
	int has_target,
	sqlparser_error_t *out_error)
{
	if (window == NULL) {
		return 0;
	}
	if (sqlparser_graph_walk_node_array_with_target_path(
		    build,
		    block_index,
		    clause,
		    window->partition_clause,
		    window->n_partition_clause,
		    target_index,
		    has_target,
		    "expression",
		    "window_partition",
		    out_error) != 0 ||
	    sqlparser_graph_walk_node_array_with_target_path(
		    build,
		    block_index,
		    clause,
		    window->order_clause,
		    window->n_order_clause,
		    target_index,
		    has_target,
		    "expression",
		    "window_order",
		    out_error) != 0 ||
	    sqlparser_graph_walk_expr_with_target_path(
		    build,
		    block_index,
		    clause,
		    window->start_offset,
		    target_index,
		    has_target,
		    "expression",
		    "window_frame",
		    0U,
		    out_error) != 0) {
		return -1;
	}
	return sqlparser_graph_walk_expr_with_target_path(
		build,
		block_index,
		clause,
		window->end_offset,
		target_index,
		has_target,
		"expression",
		"window_frame",
		1U,
		out_error);
}

static int sqlparser_graph_walk_json_agg_constructor(
	sqlparser_graph_build_t *build,
	size_t block_index,
	sqlparser_clause_kind_t clause,
	PgQuery__JsonAggConstructor *constructor,
	size_t target_index,
	int has_target,
	const char *function_name,
	size_t arg_count,
	sqlparser_error_t *out_error)
{
	size_t index;

	if (constructor == NULL) {
		return 0;
	}
	for (index = 0U; index < constructor->n_agg_order; index++) {
		if (sqlparser_graph_walk_expr_with_target_path(
			    build,
			    block_index,
			    clause,
			    constructor->agg_order[index],
			    target_index,
			    has_target,
			    "function",
			    function_name,
			    arg_count + index,
			    out_error) != 0) {
			return -1;
		}
	}
	if (sqlparser_graph_walk_expr_with_target_path(
		    build,
		    block_index,
		    clause,
		    constructor->agg_filter,
		    target_index,
		    has_target,
		    "function",
		    function_name,
		    arg_count + constructor->n_agg_order,
		    out_error) != 0) {
		return -1;
	}
	return sqlparser_graph_walk_window_def_with_target_path(
		build,
		block_index,
		clause,
		constructor->over,
		target_index,
		has_target,
		out_error);
}

static int sqlparser_graph_build_select(
	sqlparser_graph_build_t *build,
	PgQuery__SelectStmt *stmt,
	sqlparser_graph_block_kind_t kind,
	size_t *out_block_index,
	sqlparser_error_t *out_error);

static int sqlparser_graph_build_insert_dml(
	sqlparser_graph_build_t *build,
	PgQuery__InsertStmt *stmt,
	sqlparser_error_t *out_error);

static int sqlparser_graph_build_update_dml(
	sqlparser_graph_build_t *build,
	PgQuery__UpdateStmt *stmt,
	sqlparser_error_t *out_error);

static int sqlparser_graph_build_delete_dml(
	sqlparser_graph_build_t *build,
	PgQuery__DeleteStmt *stmt,
	sqlparser_error_t *out_error);

static int sqlparser_graph_build_merge_dml(
	sqlparser_graph_build_t *build,
	PgQuery__MergeStmt *stmt,
	sqlparser_error_t *out_error);

static size_t sqlparser_graph_cte_entry_hash(
	const PgQuery__CommonTableExpr *cte)
{
	uintptr_t value;

	value = (uintptr_t)cte;
	value ^= value >> 20U;
	value ^= value >> 12U;
	value ^= value >> 7U;
	value ^= value >> 4U;
	return (size_t)value;
}

static int sqlparser_graph_reserve_cte_entry_slots(
	sqlparser_graph_build_t *build,
	size_t required,
	sqlparser_error_t *out_error)
{
	size_t *slots;
	size_t capacity;
	size_t entry_index;

	if (build == NULL || required == 0U) {
		return 0;
	}
	if (build->cte_entry_slot_capacity > 0U &&
	    required <= build->cte_entry_slot_capacity / 2U) {
		return 0;
	}
	if (required > SIZE_MAX / 2U) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_RESOURCE_LIMIT, "CTE graph index is too large");
		return -1;
	}
	capacity = build->cte_entry_slot_capacity > 0U ?
		build->cte_entry_slot_capacity : 2U;
	while (capacity / 2U < required) {
		if (capacity > SIZE_MAX / 2U) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_RESOURCE_LIMIT, "CTE graph index is too large");
			return -1;
		}
		capacity *= 2U;
	}
	if (capacity > SIZE_MAX / sizeof(*slots)) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_RESOURCE_LIMIT, "CTE graph index is too large");
		return -1;
	}
	slots = (size_t *)calloc(capacity, sizeof(*slots));
	if (slots == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return -1;
	}
	for (entry_index = 0U;
	     entry_index < build->cte_entry_count;
	     entry_index++) {
		size_t slot;

		slot = sqlparser_graph_cte_entry_hash(
			build->cte_entries[entry_index].cte) & (capacity - 1U);
		while (slots[slot] != 0U) {
			slot = (slot + 1U) & (capacity - 1U);
		}
		slots[slot] = entry_index + 1U;
	}
	free(build->cte_entry_slots);
	build->cte_entry_slots = slots;
	build->cte_entry_slot_capacity = capacity;
	return 0;
}

static sqlparser_graph_cte_entry_t *sqlparser_graph_find_cte_entry(
	sqlparser_graph_build_t *build,
	PgQuery__CommonTableExpr *cte)
{
	size_t entry_index;
	size_t slot;

	if (build == NULL || cte == NULL ||
	    build->cte_entry_slot_capacity == 0U) {
		return NULL;
	}
	slot = sqlparser_graph_cte_entry_hash(cte) &
		(build->cte_entry_slot_capacity - 1U);
	while (build->cte_entry_slots[slot] != 0U) {
		entry_index = build->cte_entry_slots[slot] - 1U;
		if (entry_index < build->cte_entry_count &&
		    build->cte_entries[entry_index].cte == cte) {
			return &build->cte_entries[entry_index];
		}
		slot = (slot + 1U) &
			(build->cte_entry_slot_capacity - 1U);
	}
	return NULL;
}

static sqlparser_graph_cte_entry_t *sqlparser_graph_get_or_add_cte_entry(
	sqlparser_graph_build_t *build,
	PgQuery__CommonTableExpr *cte,
	sqlparser_error_t *out_error)
{
	sqlparser_graph_cte_entry_t *entry;
	size_t entry_index;
	size_t slot;

	entry = sqlparser_graph_find_cte_entry(build, cte);
	if (entry != NULL || build == NULL || cte == NULL) {
		return entry;
	}
	if (build->cte_entry_count == SIZE_MAX ||
	    sqlparser_graph_reserve_cte_entry_slots(
		    build,
		    build->cte_entry_count + 1U,
		    out_error) != 0 ||
	    sqlparser_query_graph_reserve_sparse_array(
		    (void **)&build->cte_entries,
		    &build->cte_entry_capacity,
		    build->cte_entry_count + 1U,
		    sizeof(*build->cte_entries),
		    out_error) != 0) {
		if (build->cte_entry_count == SIZE_MAX && out_error != NULL &&
		    out_error->code == SQLPARSER_STATUS_OK) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_RESOURCE_LIMIT, "CTE graph index is too large");
		}
		return NULL;
	}
	entry_index = build->cte_entry_count++;
	entry = &build->cte_entries[entry_index];
	memset(entry, 0, sizeof(*entry));
	entry->cte = cte;
	slot = sqlparser_graph_cte_entry_hash(cte) &
		(build->cte_entry_slot_capacity - 1U);
	while (build->cte_entry_slots[slot] != 0U) {
		slot = (slot + 1U) &
			(build->cte_entry_slot_capacity - 1U);
	}
	build->cte_entry_slots[slot] = entry_index + 1U;
	return entry;
}

typedef struct {
	sqlparser_graph_build_t *build;
	sqlparser_error_t *error;
} sqlparser_graph_dml_inventory_context_t;

static int sqlparser_graph_prepare_dml_inventory_entry(
	size_t dml_index,
	const sqlparser_dialect_dml_result_dml_t *dml,
	void *context)
{
	sqlparser_graph_dml_inventory_context_t *inventory;
	sqlparser_graph_build_t *build;
	sqlparser_graph_cte_entry_t *entry;
	sqlparser_graph_dml_t *graph_dml;

	inventory = (sqlparser_graph_dml_inventory_context_t *)context;
	build = inventory != NULL ? inventory->build : NULL;
	if (build == NULL || dml == NULL || dml->message == NULL ||
	    dml_index >= build->dml_inventory_count) {
		if (inventory != NULL) {
			sqlparser_error_set_message(inventory->error, SQLPARSER_STATUS_INTERNAL_ERROR, "PostgreSQL DML inventory is inconsistent");
		}
		return -1;
	}
	graph_dml = &build->cache->dml[
		build->statement->dml_offset + dml_index];
	build->dml_inventory[dml_index] = *dml;
	graph_dml->kind = dml->kind;
	if (dml->cte == NULL) {
		return 0;
	}
	entry = sqlparser_graph_find_cte_entry(build, dml->cte);
	if (entry != NULL && entry->has_dml_index) {
		sqlparser_error_set_message(inventory->error, SQLPARSER_STATUS_INTERNAL_ERROR, "data-modifying CTE appears more than once in the DML inventory");
		return -1;
	}
	if (entry == NULL) {
		entry = sqlparser_graph_get_or_add_cte_entry(
			build, dml->cte, inventory->error);
		if (entry == NULL) {
			return -1;
		}
	}
	entry->dml_index = dml_index;
	entry->has_dml_index = 1;
	return 0;
}

static int sqlparser_graph_prepare_dml_inventory(
	sqlparser_graph_build_t *build,
	sqlparser_error_t *out_error)
{
	sqlparser_graph_dml_inventory_context_t context;
	size_t count;
	size_t index;
	int status;

	if (build == NULL || build->handle == NULL ||
	    !sqlparser_dialect_uses_postgresql_placeholders(
		    build->handle->dialect)) {
		return 0;
	}
	count = 0U;
	status = sqlparser_dialect_postgresql_dml_result_visit(
		build->handle,
		build->statement_index,
		NULL,
		NULL,
		&count);
	if (status != 0 || count == 0U) {
		return status == 0 ? 0 : -1;
	}
	if (count > SIZE_MAX - build->cache->dml_count ||
	    sqlparser_query_graph_reserve_array(
		    (void **)&build->cache->dml,
		    &build->cache->dml_capacity,
		    build->cache->dml_count + count,
		    sizeof(*build->cache->dml),
		    out_error) != 0) {
		if (out_error != NULL && out_error->code == SQLPARSER_STATUS_OK) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_RESOURCE_LIMIT, "PostgreSQL DML inventory is too large");
		}
		return -1;
	}
	build->dml_inventory =
		(sqlparser_dialect_dml_result_dml_t *)calloc(
			count, sizeof(*build->dml_inventory));
	build->dml_inventory_built =
		(unsigned char *)calloc(count, sizeof(*build->dml_inventory_built));
	if (build->dml_inventory == NULL ||
	    build->dml_inventory_built == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return -1;
	}
	build->dml_inventory_count = count;
	for (index = 0U; index < count; index++) {
		sqlparser_graph_dml_t *dml;

		dml = &build->cache->dml[build->cache->dml_count + index];
		memset(dml, 0, sizeof(*dml));
		dml->index = index;
		dml->statement_index = build->statement_index;
	}
	build->cache->dml_count += count;
	context.build = build;
	context.error = out_error;
	status = sqlparser_dialect_postgresql_dml_result_visit(
		build->handle,
		build->statement_index,
		sqlparser_graph_prepare_dml_inventory_entry,
		&context,
		NULL);
	return status == 0 ? 0 : -1;
}

static const sqlparser_dialect_dml_result_dml_t *
sqlparser_graph_dml_inventory_at(
	const sqlparser_graph_build_t *build,
	size_t dml_index)
{
	return build != NULL && build->dml_inventory != NULL &&
		dml_index < build->dml_inventory_count ?
		&build->dml_inventory[dml_index] : NULL;
}

static int sqlparser_graph_dml_result_channel_at(
	const sqlparser_graph_build_t *build,
	size_t dml_index,
	size_t channel_index,
	sqlparser_dialect_dml_result_channel_t *out_channel)
{
	const sqlparser_dialect_dml_result_dml_t *dml;

	if (out_channel == NULL) {
		return 0;
	}
	dml = sqlparser_graph_dml_inventory_at(build, dml_index);
	if (dml != NULL) {
		memset(out_channel, 0, sizeof(*out_channel));
		if (channel_index != 0U || dml->target_count == 0U) {
			return 0;
		}
		out_channel->kind = SQLPARSER_GRAPH_DML_RESULT_CLIENT;
		out_channel->target_count = dml->target_count;
		return 1;
	}
	return build != NULL && build->handle != NULL &&
		sqlparser_dialect_dml_result_channel_at(
			build->handle,
			build->statement_index,
			dml_index,
			channel_index,
			out_channel);
}

static int sqlparser_graph_ensure_cte_block(
	sqlparser_graph_build_t *build,
	PgQuery__CommonTableExpr *cte,
	size_t *out_block_index,
	sqlparser_error_t *out_error)
{
	sqlparser_graph_cte_entry_t *entry;
	PgQuery__SelectStmt *cte_stmt;
	sqlparser_graph_dml_result_cache_t *result_cache;
	sqlparser_graph_dml_result_meta_t *meta;
	size_t dml_index;
	size_t global_dml_index;
	int has_returning;
	int requires_select_block;
	int rc;

	if (out_block_index != NULL) {
		*out_block_index = 0U;
	}
	if (cte == NULL || cte->ctequery == NULL) {
		return 0;
	}
	entry = sqlparser_graph_find_cte_entry(build, cte);
	if (entry != NULL && entry->has_block) {
		if (out_block_index != NULL) {
			*out_block_index = entry->block_index;
		}
		return 0;
	}
	if (entry != NULL && entry->built) {
		return 0;
	}
	if (entry != NULL && entry->building) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "recursive CTE block is not registered");
		return -1;
	}
	if (entry == NULL) {
		entry = sqlparser_graph_get_or_add_cte_entry(
			build, cte, out_error);
		if (entry == NULL) {
			return -1;
		}
	}
	if (!entry->has_dml_index &&
	    (cte->ctequery->node_case == PG_QUERY__NODE__NODE_INSERT_STMT ||
	     cte->ctequery->node_case == PG_QUERY__NODE__NODE_UPDATE_STMT ||
	     cte->ctequery->node_case == PG_QUERY__NODE__NODE_DELETE_STMT ||
	     cte->ctequery->node_case == PG_QUERY__NODE__NODE_MERGE_STMT)) {
		if (build->handle != NULL &&
		    sqlparser_dialect_uses_postgresql_placeholders(
			    build->handle->dialect)) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "data-modifying CTE is missing from the PostgreSQL DML inventory");
			return -1;
		}
		entry->built = 1;
		return 0;
	}
	entry->building = 1;
	dml_index = entry->has_dml_index ? entry->dml_index :
		build->cache->dml_count - build->statement->dml_offset;
	has_returning = 0;
	requires_select_block = 0;
	if (entry->has_dml_index) {
		if (build->has_claiming_dml_index) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "PostgreSQL DML inventory claim is already active");
			return -1;
		}
		build->claiming_dml_index = entry->dml_index;
		build->has_claiming_dml_index = 1;
	}
	switch (cte->ctequery->node_case) {
		case PG_QUERY__NODE__NODE_SELECT_STMT:
			cte_stmt = cte->ctequery->select_stmt;
			if (cte_stmt == NULL) {
				rc = 0;
				break;
			}
			requires_select_block = 1;
			build->registering_cte = entry;
			build->registering_cte_stmt = cte_stmt;
			rc = sqlparser_graph_build_select(
				build,
				cte_stmt,
				SQLPARSER_GRAPH_BLOCK_CTE,
				NULL,
				out_error);
			if (build->registering_cte != NULL &&
			    build->registering_cte->cte == cte) {
				build->registering_cte = NULL;
				build->registering_cte_stmt = NULL;
			}
			break;
		case PG_QUERY__NODE__NODE_INSERT_STMT:
			has_returning = cte->ctequery->insert_stmt != NULL &&
				cte->ctequery->insert_stmt->n_returning_list > 0U;
			rc = sqlparser_graph_build_insert_dml(
				build, cte->ctequery->insert_stmt, out_error);
			break;
		case PG_QUERY__NODE__NODE_UPDATE_STMT:
			has_returning = cte->ctequery->update_stmt != NULL &&
				cte->ctequery->update_stmt->n_returning_list > 0U;
			rc = sqlparser_graph_build_update_dml(
				build, cte->ctequery->update_stmt, out_error);
			break;
		case PG_QUERY__NODE__NODE_DELETE_STMT:
			has_returning = cte->ctequery->delete_stmt != NULL &&
				cte->ctequery->delete_stmt->n_returning_list > 0U;
			rc = sqlparser_graph_build_delete_dml(
				build, cte->ctequery->delete_stmt, out_error);
			break;
		case PG_QUERY__NODE__NODE_MERGE_STMT:
			has_returning = cte->ctequery->merge_stmt != NULL &&
				cte->ctequery->merge_stmt->n_returning_list > 0U;
			rc = sqlparser_graph_build_merge_dml(
				build, cte->ctequery->merge_stmt, out_error);
			break;
		default:
			rc = 0;
			break;
	}
	entry = sqlparser_graph_find_cte_entry(build, cte);
	if (rc != 0 || entry == NULL) {
		if (entry != NULL) {
			entry->building = 0;
		}
		if (rc == 0 && entry == NULL) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "CTE block was not registered");
		}
		return -1;
	}
	if (requires_select_block && !entry->has_block) {
		entry->building = 0;
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "CTE block was not registered");
		return -1;
	}
	if (entry->has_dml_index &&
	    (entry->dml_index >= build->dml_inventory_count ||
	     !build->dml_inventory_built[entry->dml_index])) {
		entry->building = 0;
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "data-modifying CTE did not claim its DML inventory slot");
		return -1;
	}
	if (has_returning) {
		result_cache = build->cache->dml_results;
		global_dml_index = build->statement->dml_offset + dml_index;
		meta = result_cache != NULL &&
			global_dml_index < result_cache->meta_count ?
			&result_cache->metas[global_dml_index] : NULL;
		if (meta != NULL &&
		    meta->dml_global_index == global_dml_index &&
		    meta->result_count == 1U &&
		    result_cache->results[meta->result_offset].kind ==
			    SQLPARSER_GRAPH_DML_RESULT_CLIENT) {
			entry->block_index =
				result_cache->results[meta->result_offset].block_index;
			entry->has_block = 1;
		}
		if (!entry->has_block) {
			entry->building = 0;
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "data-modifying CTE result block was not registered");
			return -1;
		}
	}
	entry->built = 1;
	entry->building = 0;
	if (out_block_index != NULL) {
		*out_block_index = entry->block_index;
	}
	return 0;
}

static void sqlparser_graph_register_cte_block(
	sqlparser_graph_build_t *build,
	PgQuery__SelectStmt *stmt,
	size_t block_index)
{
	if (build == NULL || build->registering_cte == NULL ||
	    build->registering_cte_stmt != stmt) {
		return;
	}
	build->registering_cte->block_index = block_index;
	build->registering_cte->has_block = 1;
	build->registering_cte = NULL;
	build->registering_cte_stmt = NULL;
}

static int sqlparser_graph_ensure_ctes(
	sqlparser_graph_build_t *build,
	PgQuery__WithClause *with_clause,
	sqlparser_error_t *out_error)
{
	size_t index;

	if (with_clause == NULL) {
		return 0;
	}
	for (index = 0U; index < with_clause->n_ctes; index++) {
		PgQuery__Node *node;

		node = with_clause->ctes[index];
		if (node == NULL ||
		    node->node_case != PG_QUERY__NODE__NODE_COMMON_TABLE_EXPR ||
		    node->common_table_expr == NULL ||
		    node->common_table_expr->ctequery == NULL) {
			continue;
		}
		if (sqlparser_graph_ensure_cte_block(
			    build,
			    node->common_table_expr,
			    NULL,
			    out_error) != 0) {
			return -1;
		}
	}
	return 0;
}

static int sqlparser_graph_build_sublink(
	sqlparser_graph_build_t *build,
	PgQuery__SubLink *sub_link,
	size_t *out_block_index,
	sqlparser_error_t *out_error)
{
	if (out_block_index != NULL) {
		*out_block_index = 0U;
	}
	if (sub_link == NULL ||
	    sub_link->subselect == NULL ||
	    sub_link->subselect->node_case != PG_QUERY__NODE__NODE_SELECT_STMT ||
	    sub_link->subselect->select_stmt == NULL) {
		return 0;
	}
	return sqlparser_graph_build_select(
		build,
		sub_link->subselect->select_stmt,
		SQLPARSER_GRAPH_BLOCK_SCALAR_SUBQUERY,
		out_block_index,
		out_error);
}

static int sqlparser_graph_walk_expr(
	sqlparser_graph_build_t *build,
	size_t block_index,
	sqlparser_clause_kind_t clause,
	PgQuery__Node *node,
	size_t target_index,
	int has_target,
	sqlparser_error_t *out_error)
{
	size_t index;

	if (node == NULL) {
		return 0;
	}
	node = sqlparser_unwrap_grouping_node(node);
	if (node == NULL) {
		return 0;
	}
	switch (node->node_case) {
		case PG_QUERY__NODE__NODE_RES_TARGET:
			if (node->res_target == NULL) {
				return 0;
			}
			if (sqlparser_graph_walk_node_array(
				    build,
				    block_index,
				    clause,
				    node->res_target->indirection,
				    node->res_target->n_indirection,
				    out_error) != 0) {
				return -1;
			}
			return sqlparser_graph_walk_expr(
				build,
				block_index,
				clause,
				node->res_target->val,
				target_index,
				has_target,
				out_error);
		case PG_QUERY__NODE__NODE_A_CONST:
		case PG_QUERY__NODE__NODE_PARAM_REF:
		case PG_QUERY__NODE__NODE_SET_TO_DEFAULT:
		{
			size_t value_index;
			int value_status;

			if (clause != SQLPARSER_CLAUSE_KIND_CONDITION &&
			    (clause != SQLPARSER_CLAUSE_KIND_SET_LIST ||
			     !sqlparser_graph_rhs_capture_is_active(
				     build,
				     block_index))) {
				return 0;
			}
			value_index = 0U;
			value_status = sqlparser_graph_record_value_node(
				build,
				block_index,
				clause,
				NULL,
				0U,
				0,
				SQLPARSER_GRAPH_FIELD_MATCH_UNKNOWN,
				node,
				NULL,
				&value_index,
				out_error);
			if (value_status < 0) {
				return -1;
			}
			return value_status > 0 &&
				sqlparser_graph_rhs_capture_is_active(
					build,
					block_index) ?
				sqlparser_graph_span_append_unique_index(
					build,
					&build->rhs_capture_assignment->rhs_values,
					value_index,
					out_error) :
				0;
		}
		case PG_QUERY__NODE__NODE_COLUMN_REF:
		{
			size_t field_index;
			int capture_field;

			capture_field =
				sqlparser_graph_rhs_capture_is_active(
					build,
					block_index) &&
				sqlparser_graph_column_ref_is_recordable_field(
					build,
					node->column_ref);
			field_index = 0U;
			if (sqlparser_graph_add_column_ref_field(
				    build,
				    block_index,
				    clause,
				    node->column_ref,
				    target_index,
				    has_target,
				    capture_field ? &field_index : NULL,
				    out_error) != 0) {
				return -1;
			}
			return capture_field ?
				sqlparser_graph_span_append_unique_index(
					build,
					&build->rhs_capture_assignment->rhs_fields,
					field_index,
					out_error) :
				0;
		}
		case PG_QUERY__NODE__NODE_A_INDIRECTION:
			if (node->a_indirection == NULL) {
				return 0;
			}
			if (sqlparser_graph_walk_expr_with_target_path(
				    build,
				    block_index,
				    clause,
				    node->a_indirection->arg,
				    target_index,
				    has_target,
				    "expression",
				    "indirection",
				    0U,
				    out_error) != 0) {
				return -1;
			}
			return sqlparser_graph_walk_node_array_with_target_path(
				build,
				block_index,
				clause,
				node->a_indirection->indirection,
				node->a_indirection->n_indirection,
				target_index,
				has_target,
				"expression",
				"indirection",
				out_error);
		case PG_QUERY__NODE__NODE_A_INDICES:
			if (node->a_indices == NULL) {
				return 0;
			}
			if (sqlparser_graph_walk_expr_with_target_path(
				    build,
				    block_index,
				    clause,
				    node->a_indices->lidx,
				    target_index,
				    has_target,
				    "expression",
				    "subscript",
				    0U,
				    out_error) != 0) {
				return -1;
			}
			return sqlparser_graph_walk_expr_with_target_path(
				build,
				block_index,
				clause,
				node->a_indices->uidx,
				target_index,
				has_target,
				"expression",
				"subscript",
				1U,
				out_error);
		case PG_QUERY__NODE__NODE_NAMED_ARG_EXPR:
			return node->named_arg_expr != NULL ?
				sqlparser_graph_walk_expr(
					build,
					block_index,
					clause,
					node->named_arg_expr->arg,
					target_index,
					has_target,
					out_error) :
				0;
		case PG_QUERY__NODE__NODE_GROUPING_FUNC:
			return node->grouping_func != NULL ?
				sqlparser_graph_walk_node_array_with_target_path(
					build,
					block_index,
					clause,
					node->grouping_func->args,
					node->grouping_func->n_args,
					target_index,
					has_target,
					"function",
					"GROUPING",
					out_error) :
				0;
		case PG_QUERY__NODE__NODE_GROUPING_SET:
			return node->grouping_set != NULL ?
				sqlparser_graph_walk_node_array(
					build,
					block_index,
					clause,
					node->grouping_set->content,
					node->grouping_set->n_content,
					out_error) :
				0;
		case PG_QUERY__NODE__NODE_MULTI_ASSIGN_REF:
			return node->multi_assign_ref != NULL &&
				       node->multi_assign_ref->colno == 1 ?
				sqlparser_graph_walk_expr(
					build,
					block_index,
					clause,
					node->multi_assign_ref->source,
					target_index,
					has_target,
					out_error) :
				0;
		case PG_QUERY__NODE__NODE_XML_EXPR:
		{
			const char *path_kind;
			const char *path_name;

			if (node->xml_expr == NULL) {
				return 0;
			}
			path_kind = "function";
			switch (node->xml_expr->op) {
				case PG_QUERY__XML_EXPR_OP__IS_XMLCONCAT:
					path_name = "XMLCONCAT";
					break;
				case PG_QUERY__XML_EXPR_OP__IS_XMLELEMENT:
					path_name = "XMLELEMENT";
					break;
				case PG_QUERY__XML_EXPR_OP__IS_XMLFOREST:
					path_name = "XMLFOREST";
					break;
				case PG_QUERY__XML_EXPR_OP__IS_XMLPARSE:
					path_name = "XMLPARSE";
					break;
				case PG_QUERY__XML_EXPR_OP__IS_XMLPI:
					path_name = "XMLPI";
					break;
				case PG_QUERY__XML_EXPR_OP__IS_XMLROOT:
					path_name = "XMLROOT";
					break;
				case PG_QUERY__XML_EXPR_OP__IS_XMLSERIALIZE:
					path_name = "XMLSERIALIZE";
					break;
				case PG_QUERY__XML_EXPR_OP__IS_DOCUMENT:
					path_kind = "expression";
					path_name = "is_document";
					break;
				default:
					path_name = "XML";
					break;
			}
			if (sqlparser_graph_walk_node_array_with_target_path(
				    build,
				    block_index,
				    clause,
				    node->xml_expr->named_args,
				    node->xml_expr->n_named_args,
				    target_index,
				    has_target,
				    path_kind,
				    path_name,
				    out_error) != 0) {
				return -1;
			}
			return sqlparser_graph_walk_node_array_with_target_path(
				build,
				block_index,
				clause,
				node->xml_expr->args,
				node->xml_expr->n_args,
				target_index,
				has_target,
				path_kind,
				path_name,
				out_error);
		}
		case PG_QUERY__NODE__NODE_JSON_VALUE_EXPR:
			return node->json_value_expr != NULL ?
				sqlparser_graph_walk_expr(
					build,
					block_index,
					clause,
					node->json_value_expr->raw_expr,
					target_index,
					has_target,
					out_error) :
				0;
		case PG_QUERY__NODE__NODE_JSON_ARGUMENT:
			return node->json_argument != NULL &&
			       node->json_argument->val != NULL ?
				sqlparser_graph_walk_expr(
					build,
					block_index,
					clause,
					node->json_argument->val->raw_expr,
					target_index,
					has_target,
					out_error) :
				0;
		case PG_QUERY__NODE__NODE_JSON_PARSE_EXPR:
			return node->json_parse_expr != NULL &&
			       node->json_parse_expr->expr != NULL ?
				sqlparser_graph_walk_expr_with_target_path(
					build,
					block_index,
					clause,
					node->json_parse_expr->expr->raw_expr,
					target_index,
					has_target,
					"function",
					"JSON",
					0U,
					out_error) :
				0;
		case PG_QUERY__NODE__NODE_JSON_SCALAR_EXPR:
			return node->json_scalar_expr != NULL ?
				sqlparser_graph_walk_expr_with_target_path(
					build,
					block_index,
					clause,
					node->json_scalar_expr->expr,
					target_index,
					has_target,
					"function",
					"JSON_SCALAR",
					0U,
					out_error) :
				0;
		case PG_QUERY__NODE__NODE_JSON_SERIALIZE_EXPR:
			return node->json_serialize_expr != NULL &&
			       node->json_serialize_expr->expr != NULL ?
				sqlparser_graph_walk_expr_with_target_path(
					build,
					block_index,
					clause,
					node->json_serialize_expr->expr->raw_expr,
					target_index,
					has_target,
					"function",
					"JSON_SERIALIZE",
					0U,
					out_error) :
				0;
		case PG_QUERY__NODE__NODE_JSON_IS_PREDICATE:
			return node->json_is_predicate != NULL ?
				sqlparser_graph_walk_expr_with_target_path(
					build,
					block_index,
					clause,
					node->json_is_predicate->expr,
					target_index,
					has_target,
					"expression",
					"is_json",
					0U,
					out_error) :
				0;
		case PG_QUERY__NODE__NODE_XML_SERIALIZE:
			return node->xml_serialize != NULL ?
				sqlparser_graph_walk_expr_with_target_path(
					build,
					block_index,
					clause,
					node->xml_serialize->expr,
					target_index,
					has_target,
					"function",
					"XMLSERIALIZE",
					0U,
					out_error) :
				0;
		case PG_QUERY__NODE__NODE_RANGE_TABLE_FUNC_COL:
			if (node->range_table_func_col == NULL) {
				return 0;
			}
			if (sqlparser_graph_walk_expr(
				    build,
				    block_index,
				    clause,
				    node->range_table_func_col->colexpr,
				    target_index,
				    has_target,
				    out_error) != 0) {
				return -1;
			}
			return sqlparser_graph_walk_expr(
				build,
				block_index,
				clause,
				node->range_table_func_col->coldefexpr,
				target_index,
				has_target,
				out_error);
		case PG_QUERY__NODE__NODE_RANGE_TABLE_FUNC:
			if (node->range_table_func == NULL) {
				return 0;
			}
			if (sqlparser_graph_walk_expr(
				    build,
				    block_index,
				    clause,
				    node->range_table_func->docexpr,
				    target_index,
				    has_target,
				    out_error) != 0 ||
			    sqlparser_graph_walk_expr(
				    build,
				    block_index,
				    clause,
				    node->range_table_func->rowexpr,
				    target_index,
				    has_target,
				    out_error) != 0 ||
			    sqlparser_graph_walk_node_array(
				    build,
				    block_index,
				    clause,
				    node->range_table_func->namespaces,
				    node->range_table_func->n_namespaces,
				    out_error) != 0) {
				return -1;
			}
			return sqlparser_graph_walk_node_array(
				build,
				block_index,
				clause,
				node->range_table_func->columns,
				node->range_table_func->n_columns,
				out_error);
		case PG_QUERY__NODE__NODE_JSON_TABLE_PATH_SPEC:
			return node->json_table_path_spec != NULL ?
				sqlparser_graph_walk_expr(
					build,
					block_index,
					clause,
					node->json_table_path_spec->string,
					target_index,
					has_target,
					out_error) :
				0;
		case PG_QUERY__NODE__NODE_JSON_TABLE_COLUMN:
			if (node->json_table_column == NULL) {
				return 0;
			}
			if (sqlparser_graph_walk_node_array(
				    build,
				    block_index,
				    clause,
				    node->json_table_column->columns,
				    node->json_table_column->n_columns,
				    out_error) != 0 ||
			    sqlparser_graph_walk_expr(
				    build,
				    block_index,
				    clause,
				    node->json_table_column->on_empty != NULL ?
					    node->json_table_column->on_empty->expr : NULL,
				    target_index,
				    has_target,
				    out_error) != 0) {
				return -1;
			}
			return sqlparser_graph_walk_expr(
				build,
				block_index,
				clause,
				node->json_table_column->on_error != NULL ?
					node->json_table_column->on_error->expr : NULL,
				target_index,
				has_target,
				out_error);
		case PG_QUERY__NODE__NODE_JSON_TABLE:
			if (node->json_table == NULL) {
				return 0;
			}
			if ((node->json_table->context_item != NULL &&
			     sqlparser_graph_walk_expr(
				     build,
				     block_index,
				     clause,
				     node->json_table->context_item->raw_expr,
				     target_index,
				     has_target,
				     out_error) != 0) ||
			    (node->json_table->pathspec != NULL &&
			     sqlparser_graph_walk_expr(
				     build,
				     block_index,
				     clause,
				     node->json_table->pathspec->string,
				     target_index,
				     has_target,
				     out_error) != 0) ||
			    sqlparser_graph_walk_node_array(
				    build,
				    block_index,
				    clause,
				    node->json_table->passing,
				    node->json_table->n_passing,
				    out_error) != 0 ||
			    sqlparser_graph_walk_node_array(
				    build,
				    block_index,
				    clause,
				    node->json_table->columns,
				    node->json_table->n_columns,
				    out_error) != 0) {
				return -1;
			}
			return sqlparser_graph_walk_expr(
				build,
				block_index,
				clause,
				node->json_table->on_error != NULL ?
					node->json_table->on_error->expr : NULL,
				target_index,
				has_target,
				out_error);
		case PG_QUERY__NODE__NODE_JSON_FUNC_EXPR:
		{
			const char *function_name;
			size_t next_arg;

			if (node->json_func_expr == NULL) {
				return 0;
			}
			switch (node->json_func_expr->op) {
				case PG_QUERY__JSON_EXPR_OP__JSON_EXISTS_OP:
					function_name = "JSON_EXISTS";
					break;
				case PG_QUERY__JSON_EXPR_OP__JSON_QUERY_OP:
					function_name = "JSON_QUERY";
					break;
				case PG_QUERY__JSON_EXPR_OP__JSON_VALUE_OP:
					function_name = "JSON_VALUE";
					break;
				default:
					return 0;
			}
			if (node->json_func_expr->context_item != NULL &&
			    sqlparser_graph_walk_expr_with_target_path(
				    build,
				    block_index,
				    clause,
				    node->json_func_expr->context_item->raw_expr,
				    target_index,
				    has_target,
				    "function",
				    function_name,
				    0U,
					out_error) != 0) {
				return -1;
			}
			if (sqlparser_graph_walk_expr_with_target_path(
				    build,
				    block_index,
				    clause,
				    node->json_func_expr->pathspec,
				    target_index,
				    has_target,
				    "function",
				    function_name,
				    1U,
				    out_error) != 0) {
				return -1;
			}
			for (index = 0U;
			     index < node->json_func_expr->n_passing;
			     index++) {
				if (sqlparser_graph_walk_expr_with_target_path(
					    build,
					    block_index,
					    clause,
					    node->json_func_expr->passing[index],
					    target_index,
					    has_target,
					    "function",
					    function_name,
					    2U + index,
					    out_error) != 0) {
					return -1;
				}
			}
			next_arg = 2U + node->json_func_expr->n_passing;
			if (sqlparser_graph_walk_expr_with_target_path(
				    build,
				    block_index,
				    clause,
				    node->json_func_expr->on_empty != NULL ?
					    node->json_func_expr->on_empty->expr : NULL,
				    target_index,
				    has_target,
				    "function",
				    function_name,
				    next_arg,
				    out_error) != 0) {
				return -1;
			}
			return sqlparser_graph_walk_expr_with_target_path(
				build,
				block_index,
				clause,
				node->json_func_expr->on_error != NULL ?
					node->json_func_expr->on_error->expr : NULL,
				target_index,
				has_target,
				"function",
				function_name,
				next_arg + 1U,
				out_error);
		}
		case PG_QUERY__NODE__NODE_JSON_OBJECT_CONSTRUCTOR:
			if (node->json_object_constructor == NULL) {
				return 0;
			}
			for (index = 0U; index < node->json_object_constructor->n_exprs; index++) {
				PgQuery__Node *item;
				PgQuery__JsonKeyValue *key_value;

				item = node->json_object_constructor->exprs[index];
				if (item == NULL ||
				    item->node_case != PG_QUERY__NODE__NODE_JSON_KEY_VALUE ||
				    item->json_key_value == NULL) {
					continue;
				}
				key_value = item->json_key_value;
				if (sqlparser_graph_walk_expr_with_target_path(
					    build,
					    block_index,
					    clause,
					    key_value->key,
					    target_index,
					    has_target,
					    "function",
					    "JSON_OBJECT",
					    index * 2U,
					    out_error) != 0 ||
				    sqlparser_graph_walk_expr_with_target_path(
					    build,
					    block_index,
					    clause,
					    key_value->value != NULL ? key_value->value->raw_expr : NULL,
					    target_index,
					    has_target,
					    "function",
					    "JSON_OBJECT",
					    index * 2U + 1U,
					    out_error) != 0) {
					return -1;
				}
			}
			return 0;
		case PG_QUERY__NODE__NODE_JSON_ARRAY_CONSTRUCTOR:
			return node->json_array_constructor != NULL ?
				sqlparser_graph_walk_node_array_with_target_path(
					build,
					block_index,
					clause,
					node->json_array_constructor->exprs,
					node->json_array_constructor->n_exprs,
					target_index,
					has_target,
					"function",
					"JSON_ARRAY",
					out_error) :
				0;
		case PG_QUERY__NODE__NODE_JSON_ARRAY_QUERY_CONSTRUCTOR:
		{
			PgQuery__Node *query;
			sqlparser_graph_dml_assignment_t *saved_rhs_capture_assignment;
			size_t saved_rhs_capture_block_index;
			size_t source_block_index;
			int capture_source_block;
			int rc;

			query = node->json_array_query_constructor != NULL ?
				node->json_array_query_constructor->query : NULL;
			if (query == NULL) {
				return 0;
			}
			source_block_index = 0U;
			if (query->node_case == PG_QUERY__NODE__NODE_SELECT_STMT &&
			    query->select_stmt != NULL) {
				capture_source_block =
					sqlparser_graph_rhs_capture_is_active(
						build,
						block_index);
				saved_rhs_capture_assignment =
					build->rhs_capture_assignment;
				saved_rhs_capture_block_index =
					build->rhs_capture_block_index;
				build->rhs_capture_assignment = NULL;
				build->rhs_capture_block_index = 0U;
				rc = sqlparser_graph_build_select(
					build,
					query->select_stmt,
					SQLPARSER_GRAPH_BLOCK_SCALAR_SUBQUERY,
					&source_block_index,
					out_error);
				build->rhs_capture_assignment =
					saved_rhs_capture_assignment;
				build->rhs_capture_block_index =
					saved_rhs_capture_block_index;
				if (rc == 0 && capture_source_block &&
				    sqlparser_graph_span_append_unique_index(
					    build,
					    &saved_rhs_capture_assignment->rhs_blocks,
					    source_block_index,
					    out_error) != 0) {
					return -1;
				}
				if (rc == 0 && has_target) {
					sqlparser_graph_target_cache_t *target;

					target = sqlparser_graph_target_by_local(
						build,
						target_index);
					if (target != NULL) {
						target->source_block_index =
							source_block_index;
						target->flags |=
							SQLPARSER_GRAPH_TARGET_HAS_SOURCE_BLOCK;
					}
				}
				return rc;
			}
			return sqlparser_graph_walk_expr(
				build,
				block_index,
				clause,
				query,
				target_index,
				has_target,
				out_error);
		}
		case PG_QUERY__NODE__NODE_JSON_OBJECT_AGG:
			if (node->json_object_agg == NULL) {
				return 0;
			}
			if (node->json_object_agg->arg != NULL &&
			    (sqlparser_graph_walk_expr_with_target_path(
				    build,
				    block_index,
				    clause,
				    node->json_object_agg->arg->key,
				    target_index,
				    has_target,
				    "function",
				    "JSON_OBJECTAGG",
				    0U,
				    out_error) != 0 ||
			     sqlparser_graph_walk_expr_with_target_path(
				     build,
				     block_index,
				     clause,
				     node->json_object_agg->arg->value != NULL ?
					     node->json_object_agg->arg->value->raw_expr : NULL,
				     target_index,
				     has_target,
				     "function",
				     "JSON_OBJECTAGG",
				     1U,
				     out_error) != 0)) {
				return -1;
			}
			return sqlparser_graph_walk_json_agg_constructor(
				build,
				block_index,
				clause,
				node->json_object_agg->constructor,
				target_index,
				has_target,
				"JSON_OBJECTAGG",
				2U,
				out_error);
		case PG_QUERY__NODE__NODE_JSON_ARRAY_AGG:
			if (node->json_array_agg == NULL) {
				return 0;
			}
			if (node->json_array_agg->arg != NULL &&
			    sqlparser_graph_walk_expr_with_target_path(
					build,
					block_index,
					clause,
					node->json_array_agg->arg->raw_expr,
					target_index,
					has_target,
					"function",
					"JSON_ARRAYAGG",
					0U,
					out_error) != 0) {
				return -1;
			}
			return sqlparser_graph_walk_json_agg_constructor(
				build,
				block_index,
				clause,
				node->json_array_agg->constructor,
				target_index,
				has_target,
				"JSON_ARRAYAGG",
				1U,
				out_error);
		case PG_QUERY__NODE__NODE_A_EXPR:
		{
			const char *operator_name;
			PgQuery__Node *prefix_operand;
			int left_value_status;
			int right_value_status;
			int status;
			unsigned int saved_prior_depth;
			size_t saved_count;

			if (node->a_expr == NULL) {
				return 0;
			}
			prefix_operand = NULL;
			if (sqlparser_graph_prefix_a_expr_operand(
				    node,
				    "PRIOR",
				    &prefix_operand)) {
				saved_prior_depth = build->hierarchy_prior_depth;
				build->hierarchy_prior_depth++;
				status = sqlparser_graph_walk_expr(
					build,
					block_index,
					clause,
					prefix_operand,
					target_index,
					has_target,
					out_error);
				build->hierarchy_prior_depth = saved_prior_depth;
				return status;
			}
			prefix_operand = NULL;
			if (sqlparser_graph_prefix_a_expr_operand(
				    node,
				    "CONNECT_BY_ROOT",
				    &prefix_operand)) {
				return sqlparser_graph_walk_expr_with_target_path(
					build,
					block_index,
					clause,
					prefix_operand,
					target_index,
					has_target,
					"operator",
					"CONNECT_BY_ROOT",
					0U,
					out_error);
			}
			if (build->collect_relation_bindings) {
				if (sqlparser_graph_walk_expr(
					    build,
					    block_index,
					    clause,
					    node->a_expr->lexpr,
					    target_index,
					    has_target,
					    out_error) != 0) {
					return -1;
				}
				return sqlparser_graph_walk_expr(
					build,
					block_index,
					clause,
					node->a_expr->rexpr,
					target_index,
					has_target,
					out_error);
			}
			operator_name = sqlparser_a_expr_operator_name(node->a_expr);
			saved_count = sqlparser_graph_target_path_save(build);
			if (has_target) {
				(void)sqlparser_graph_target_path_push(build, "expression", operator_name, 0U);
			}
			left_value_status = sqlparser_graph_record_predicate_value(
				    build,
				    block_index,
				    clause,
				    operator_name,
				    node->a_expr->lexpr,
				    node->a_expr->rexpr,
				    target_index,
				    has_target,
				    node->a_expr,
				    out_error);
			sqlparser_graph_target_path_restore(build, saved_count);
			if (left_value_status < 0) {
				return -1;
			}
			saved_count = sqlparser_graph_target_path_save(build);
			if (has_target) {
				(void)sqlparser_graph_target_path_push(build, "expression", operator_name, 1U);
			}
				right_value_status = sqlparser_graph_record_predicate_value(
					    build,
					    block_index,
					    clause,
				    operator_name,
				    node->a_expr->rexpr,
				    node->a_expr->lexpr,
				    target_index,
				    has_target,
					    node->a_expr,
					    out_error);
				sqlparser_graph_target_path_restore(build, saved_count);
				if (right_value_status < 0) {
					return -1;
				}
				if (left_value_status > 0 || right_value_status > 0) {
					if (sqlparser_graph_walk_expr_with_target_path(
						    build,
						    block_index,
						    clause,
						    node->a_expr->lexpr,
						    target_index,
						    has_target,
						    "expression",
						    operator_name,
						    0U,
						    out_error) != 0) {
						return -1;
					}
					return sqlparser_graph_walk_expr_with_target_path(
						build,
						block_index,
						clause,
						node->a_expr->rexpr,
						target_index,
						has_target,
						"expression",
						operator_name,
						1U,
						out_error);
				}
			if (sqlparser_graph_walk_expr_with_target_path(
				    build,
				    block_index,
				    clause,
				    node->a_expr->lexpr,
				    target_index,
				    has_target,
				    "expression",
				    operator_name,
				    0U,
				    out_error) != 0) {
				return -1;
			}
			return sqlparser_graph_walk_expr_with_target_path(
				build,
				block_index,
				clause,
				node->a_expr->rexpr,
				target_index,
				has_target,
				"expression",
				operator_name,
				1U,
				out_error);
		}
		case PG_QUERY__NODE__NODE_BOOL_EXPR:
			return node->bool_expr != NULL ?
				sqlparser_graph_walk_node_array_with_target_path(
					build,
					block_index,
					clause,
					node->bool_expr->args,
					node->bool_expr->n_args,
					target_index,
					has_target,
					"expression",
					sqlparser_view_bool_expr_name(node->bool_expr),
					out_error) :
				0;
		case PG_QUERY__NODE__NODE_FUNC_CALL:
			if (node->func_call != NULL) {
				char *func_name;
				int rc;

				if (sqlparser_graph_func_call_is_dml_join_wrapper(
					    build, node->func_call)) {
					return sqlparser_graph_walk_node_array_with_target_path(
						build,
						block_index,
						SQLPARSER_CLAUSE_KIND_ON,
						node->func_call->args,
						node->func_call->n_args,
						target_index,
						has_target,
						NULL,
						NULL,
						out_error);
				}
				func_name = sqlparser_view_func_call_name_dup(node->func_call);
				rc = 0;
				for (index = 0U; index < node->func_call->n_args; index++) {
					if (sqlparser_graph_func_arg_is_non_field(
						    build, node->func_call, index)) {
						continue;
					}
					if (sqlparser_graph_walk_expr_with_target_path(
						    build,
						    block_index,
						    clause,
						    node->func_call->args[index],
						    target_index,
						    has_target,
						    "function",
						    func_name,
						    index,
						    out_error) != 0) {
						rc = -1;
						break;
					}
				}
				if (rc == 0 && node->func_call->agg_order != NULL) {
					for (index = 0U; index < node->func_call->n_agg_order; index++) {
						if (sqlparser_graph_walk_expr_with_target_path(
							    build,
							    block_index,
							    clause,
							    node->func_call->agg_order[index],
							    target_index,
							    has_target,
							    "function",
							    func_name,
							    node->func_call->n_args + index,
							    out_error) != 0) {
							rc = -1;
							break;
						}
					}
				}
				if (rc == 0 && node->func_call->agg_filter != NULL) {
					rc = sqlparser_graph_walk_expr_with_target_path(
						build,
						block_index,
						clause,
						node->func_call->agg_filter,
						target_index,
						has_target,
						"function",
						func_name,
						node->func_call->n_args +
							node->func_call->n_agg_order,
						out_error);
				}
				if (rc == 0 && node->func_call->over != NULL) {
					rc = sqlparser_graph_walk_window_def_with_target_path(
						build,
						block_index,
						clause,
						node->func_call->over,
						target_index,
						has_target,
						out_error);
				}
				free(func_name);
				return rc;
			}
			return 0;
		case PG_QUERY__NODE__NODE_TYPE_CAST:
			return node->type_cast != NULL ?
				sqlparser_graph_walk_expr_with_target_path(
					build,
					block_index,
					clause,
					node->type_cast->arg,
					target_index,
					has_target,
					"function",
					"CAST",
					0U,
					out_error) :
				0;
		case PG_QUERY__NODE__NODE_COLLATE_CLAUSE:
			return node->collate_clause != NULL ?
				sqlparser_graph_walk_expr_with_target_path(
					build,
					block_index,
					clause,
					node->collate_clause->arg,
					target_index,
					has_target,
					"expression",
					"collate",
					0U,
					out_error) :
				0;
		case PG_QUERY__NODE__NODE_A_ARRAY_EXPR:
			return node->a_array_expr != NULL ?
				sqlparser_graph_walk_node_array_with_target_path(
					build,
					block_index,
					clause,
					node->a_array_expr->elements,
					node->a_array_expr->n_elements,
					target_index,
					has_target,
					"expression",
					"array",
					out_error) :
				0;
		case PG_QUERY__NODE__NODE_ARRAY_EXPR:
			return node->array_expr != NULL ?
				sqlparser_graph_walk_node_array_with_target_path(
					build,
					block_index,
					clause,
					node->array_expr->elements,
					node->array_expr->n_elements,
					target_index,
					has_target,
					"expression",
					"array",
					out_error) :
				0;
		case PG_QUERY__NODE__NODE_COALESCE_EXPR:
			return node->coalesce_expr != NULL ?
				sqlparser_graph_walk_node_array_with_target_path(
					build,
					block_index,
					clause,
					node->coalesce_expr->args,
					node->coalesce_expr->n_args,
					target_index,
					has_target,
					"function",
					"COALESCE",
					out_error) :
				0;
		case PG_QUERY__NODE__NODE_NULL_TEST:
			return node->null_test != NULL ?
				sqlparser_graph_walk_expr_with_target_path(
					build,
					block_index,
					clause,
					node->null_test->arg,
					target_index,
					has_target,
					"expression",
					"is_null",
					0U,
					out_error) :
				0;
		case PG_QUERY__NODE__NODE_BOOLEAN_TEST:
			return node->boolean_test != NULL ?
				sqlparser_graph_walk_expr_with_target_path(
					build,
					block_index,
					clause,
					node->boolean_test->arg,
					target_index,
					has_target,
					"expression",
					"boolean_test",
					0U,
					out_error) :
				0;
		case PG_QUERY__NODE__NODE_CASE_EXPR:
			if (node->case_expr == NULL) {
				return 0;
			}
			if (sqlparser_graph_walk_expr_with_target_path(
				    build,
				    block_index,
				    clause,
				    node->case_expr->arg,
				    target_index,
				    has_target,
				    "expression",
				    "case_when",
				    0U,
				    out_error) != 0 ||
			    sqlparser_graph_walk_node_array_with_target_path(
				    build,
				    block_index,
				    clause,
				    node->case_expr->args,
				    node->case_expr->n_args,
				    target_index,
				    has_target,
				    "expression",
				    "case_when",
				    out_error) != 0) {
				return -1;
			}
			return sqlparser_graph_walk_expr_with_target_path(
				build,
				block_index,
				clause,
				node->case_expr->defresult,
				target_index,
				has_target,
				"expression",
				"case_when",
				node->case_expr->n_args,
				out_error);
		case PG_QUERY__NODE__NODE_CASE_WHEN:
			if (node->case_when == NULL) {
				return 0;
			}
			return sqlparser_graph_walk_expr_with_target_path(
					build,
					block_index,
					clause,
					node->case_when->expr,
					target_index,
					has_target,
					"expression",
					"case_when",
					0U,
					out_error) != 0 ||
					sqlparser_graph_walk_expr_with_target_path(
						build,
						block_index,
						clause,
						node->case_when->result,
						target_index,
						has_target,
						"expression",
						"case_when",
						1U,
						out_error) != 0 ?
				-1 :
				0;
		case PG_QUERY__NODE__NODE_ROW_EXPR:
			return node->row_expr != NULL ?
				sqlparser_graph_walk_node_array_with_target_path(
					build,
					block_index,
					clause,
					node->row_expr->args,
					node->row_expr->n_args,
					target_index,
					has_target,
					"expression",
					"row",
					out_error) :
				0;
		case PG_QUERY__NODE__NODE_MIN_MAX_EXPR:
			return node->min_max_expr != NULL ?
				sqlparser_graph_walk_node_array_with_target_path(
					build,
					block_index,
					clause,
					node->min_max_expr->args,
					node->min_max_expr->n_args,
					target_index,
					has_target,
					"function",
					sqlparser_view_min_max_name(node->min_max_expr),
					out_error) :
				0;
		case PG_QUERY__NODE__NODE_SUB_LINK:
		{
			sqlparser_graph_dml_assignment_t *saved_rhs_capture_assignment;
			size_t saved_rhs_capture_block_index;
			size_t source_block_index;
			int capture_source_block;
			int rc;

			if (node->sub_link == NULL) {
				return 0;
			}
			if (sqlparser_graph_walk_expr(
				    build,
				    block_index,
				    clause,
				    node->sub_link->testexpr,
				    target_index,
				    has_target,
				    out_error) != 0) {
				return -1;
			}
			capture_source_block =
				sqlparser_graph_rhs_capture_is_active(
					build,
					block_index) &&
				node->sub_link->subselect != NULL &&
				node->sub_link->subselect->node_case ==
					PG_QUERY__NODE__NODE_SELECT_STMT &&
				node->sub_link->subselect->select_stmt != NULL;
			saved_rhs_capture_assignment = build->rhs_capture_assignment;
			saved_rhs_capture_block_index = build->rhs_capture_block_index;
			build->rhs_capture_assignment = NULL;
			build->rhs_capture_block_index = 0U;
			source_block_index = 0U;
			rc = sqlparser_graph_build_sublink(
				build,
				node->sub_link,
				capture_source_block ? &source_block_index : NULL,
				out_error);
			build->rhs_capture_assignment = saved_rhs_capture_assignment;
			build->rhs_capture_block_index = saved_rhs_capture_block_index;
			if (rc != 0) {
				return -1;
			}
			return capture_source_block ?
				sqlparser_graph_span_append_unique_index(
					build,
					&saved_rhs_capture_assignment->rhs_blocks,
					source_block_index,
					out_error) :
				0;
		}
		case PG_QUERY__NODE__NODE_SORT_BY:
			return node->sort_by != NULL ?
				sqlparser_graph_walk_expr(build, block_index, clause, node->sort_by->node, target_index, has_target, out_error) :
				0;
		case PG_QUERY__NODE__NODE_LIST:
			if (node->list == NULL) {
				return 0;
			}
			for (index = 0U; index < node->list->n_items; index++) {
				if (sqlparser_graph_walk_expr(build, block_index, clause, node->list->items[index], target_index, has_target, out_error) != 0) {
					return -1;
				}
			}
			return 0;
		default:
			return 0;
	}
}

static int sqlparser_graph_add_star_relations(
	sqlparser_graph_build_t *build,
	size_t block_index,
	const char *qualifier,
	PgQuery__ColumnRef *column_ref,
	sqlparser_index_span_t *out_span,
	sqlparser_error_t *out_error)
{
	size_t index;

	if (out_span == NULL) {
		return -1;
	}
	memset(out_span, 0, sizeof(*out_span));
	if (qualifier != NULL && qualifier[0] != '\0') {
		size_t relation_index;
		int has_relation;

		relation_index = 0U;
		has_relation = 0;
		if (sqlparser_graph_resolve_relation(
			    build,
			    block_index,
			    qualifier,
			    column_ref,
			    &relation_index,
			    &has_relation,
			    out_span,
			    out_error) != 0) {
			return -1;
		}
		if (!has_relation) {
			return 0;
		}
		return sqlparser_graph_span_append_index(
			       build,
			       out_span,
			       relation_index,
			       out_error) == 0 ? 0 : -1;
	}
	for (index = 0U; index < sqlparser_graph_local_relation_count(build); index++) {
		sqlparser_graph_relation_t *relation;

		relation = sqlparser_graph_relation_by_local(build, index);
		if (relation == NULL || relation->block_index != block_index) {
			continue;
		}
		if (sqlparser_graph_span_append_index(build, out_span, index, out_error) != 0) {
			return -1;
		}
	}
	return 0;
}

static void sqlparser_graph_set_star_target_source_block(
	sqlparser_graph_build_t *build,
	sqlparser_graph_target_t *target)
{
	size_t relation_index;
	sqlparser_graph_relation_t *relation;

	if (build == NULL || build->cache == NULL || target == NULL ||
	    target->star_relations.count != 1U ||
	    target->star_relations.offset >= build->cache->index_pool_count) {
		return;
	}
	relation_index = build->cache->index_pool[target->star_relations.offset];
	relation = sqlparser_graph_relation_by_local(build, relation_index);
	if (relation == NULL || !relation->has_source_block) {
		return;
	}
	target->source_block_index = relation->source_block_index;
	target->has_source_block = 1;
}

static int sqlparser_graph_target_list_boundary(
	const char *sql,
	size_t start,
	size_t end)
{
	static const char *const words[] = {
		"except", "fetch", "for", "from", "group", "having",
		"intersect", "into", "limit", "offset", "order", "union",
		"where"
	};
	size_t index;

	for (index = 0U; index < sizeof(words) / sizeof(words[0]); index++) {
		if (sqlparser_identifier_token_is_word(
			    sql,
			    start,
			    end,
			    words[index])) {
			return 1;
		}
	}
	return 0;
}

static sqlparser_identifier_source_t
sqlparser_graph_res_target_identifier_source(
	const sqlparser_graph_build_t *build,
	const PgQuery__ResTarget *target)
{
	sqlparser_identifier_source_t last_identifier;
	sqlparser_identifier_source_t source;
	const char *sql;
	size_t bracket_depth;
	size_t depth;
	size_t length;
	size_t position;
	int expect_alias;

	memset(&last_identifier, 0, sizeof(last_identifier));
	memset(&source, 0, sizeof(source));
	if (build == NULL || build->handle == NULL ||
	    target == NULL || target->name == NULL ||
	    target->name[0] == '\0') {
		return source;
	}
	if (target->location < 0) {
		(void)sqlparser_identifier_component_source(
			build->handle,
			target->location,
			0U,
			&source,
			NULL);
		return source;
	}
	sql = build->handle->parser_sql;
	length = build->handle->parser_sql_len;
	position = (size_t)target->location;
	if (position >= length) {
		return source;
	}
	bracket_depth = 0U;
	depth = 0U;
	expect_alias = 0;
	while (position < length) {
		size_t skipped;
		size_t token_end;
		int quoted;

		position = sqlparser_identifier_skip_trivia(
			sql,
			length,
			position);
		if (position >= length ||
		    (depth == 0U && bracket_depth == 0U &&
		     (sql[position] == ',' || sql[position] == ';'))) {
			break;
		}
		if (sql[position] == '(') {
			depth++;
			position++;
			continue;
		}
		if (sql[position] == '[') {
			bracket_depth++;
			position++;
			continue;
		}
		if (sql[position] == ']') {
			if (bracket_depth > 0U) {
				bracket_depth--;
			}
			position++;
			continue;
		}
		if (sql[position] == ')') {
			if (depth == 0U) {
				break;
			}
			depth--;
			position++;
			continue;
		}
		if (sql[position] != '"' &&
		    !((sql[position] == 'U' || sql[position] == 'u') &&
		      position + 2U < length &&
		      sql[position + 1U] == '&' &&
		      sql[position + 2U] == '"')) {
			skipped =
				sqlparser_public_skip_quoted_or_comment(
					SQLPARSER_DIALECT_POSTGRESQL,
					sql,
					position);
			if (skipped != position) {
				position = skipped;
				continue;
			}
		}
		if (!sqlparser_identifier_token(
			    sql,
			    length,
			    position,
			    &token_end,
			    &quoted)) {
			position++;
			continue;
		}
		if (depth == 0U && bracket_depth == 0U &&
		    expect_alias) {
			source.known = 1;
			source.quoted = quoted;
			return source;
		}
		if (depth == 0U && bracket_depth == 0U &&
		    !quoted &&
		    sqlparser_identifier_token_is_word(
			    sql,
			    position,
			    token_end,
			    "as")) {
			expect_alias = 1;
			position = token_end;
			continue;
		}
		if (depth == 0U && bracket_depth == 0U && !quoted &&
		    sqlparser_graph_target_list_boundary(
			    sql,
			    position,
			    token_end)) {
			break;
		}
		if (depth == 0U && bracket_depth == 0U) {
			last_identifier.known = 1;
			last_identifier.quoted = quoted;
		}
		if (sqlparser_identifier_token_value_equal(
			    sql,
			    position,
			    token_end,
			    quoted,
			    target->name)) {
			source.known = 1;
			source.quoted = quoted;
		}
		position = token_end;
	}
	return source.known ? source : last_identifier;
}

static int sqlparser_graph_build_target(
	sqlparser_graph_build_t *build,
	size_t block_index,
	sqlparser_selector_kind_t target_list_selector_kind,
	sqlparser_selector_kind_t target_selector_kind,
	size_t selector_item_index,
	size_t selector_row_index,
	size_t ordinal,
	sqlparser_clause_kind_t clause,
	PgQuery__ResTarget *res_target,
	size_t *out_target_index,
	sqlparser_error_t *out_error)
{
	sqlparser_identifier_source_t output_identifier;
	sqlparser_graph_target_t target;
	PgQuery__Node *expr;
	const char *name;
	const char *qualifier;
	size_t target_index;
	size_t field_index;

	memset(&output_identifier, 0, sizeof(output_identifier));
	memset(&target, 0, sizeof(target));
	target.block_index = block_index;
	target.ordinal = ordinal;
	target.output_name = res_target != NULL && res_target->name != NULL && res_target->name[0] != '\0' ? res_target->name : NULL;
	if (target.output_name != NULL) {
		output_identifier =
			sqlparser_graph_res_target_identifier_source(
				build,
				res_target);
	}
	target.target_list_selector.kind = target_list_selector_kind;
	target.target_list_selector.statement_index = build->statement_index;
	target.target_list_selector.item_index = selector_item_index;
	target.target_list_selector.row_index = selector_row_index;
	target.has_target_list_selector = 1;
	target.selector.kind = target_selector_kind;
	target.selector.statement_index = build->statement_index;
	target.selector.item_index = selector_item_index;
	target.selector.row_index = selector_row_index;
	target.selector.column_index = ordinal;
	target.has_selector = 1;
	expr = res_target != NULL ? res_target->val : NULL;
	expr = sqlparser_unwrap_grouping_node(expr);
	if (expr != NULL && expr->node_case == PG_QUERY__NODE__NODE_COLUMN_REF && expr->column_ref != NULL) {
		name = sqlparser_graph_column_ref_part(expr->column_ref, 0U);
		qualifier = sqlparser_graph_column_ref_part(expr->column_ref, 1U);
		if (target.output_name == NULL) {
			output_identifier = sqlparser_graph_column_ref_part_source(
				build->handle,
				expr->column_ref,
				0U);
		}
		if (name != NULL && strcmp(name, "*") == 0) {
			size_t resolution_block;
			size_t star_target_index;

			target.kind = qualifier != NULL && qualifier[0] != '\0' ?
				SQLPARSER_GRAPH_TARGET_QUALIFIED_STAR :
				SQLPARSER_GRAPH_TARGET_STAR;
			resolution_block = build->building_dml_result ?
				build->dml_result_scope_block_index : block_index;
			if (build->building_dml_result &&
			    build->handle != NULL &&
			    sqlparser_dialect_uses_postgresql_placeholders(
				    build->handle->dialect) &&
			    qualifier == NULL) {
				if (build->dml_result_has_target_relation &&
				    sqlparser_graph_span_append_index(
					    build,
					    &target.star_relations,
					    build->dml_result_target_relation_index,
					    out_error) != 0) {
					return -1;
				}
			} else if (build->building_dml_result &&
			    build->handle != NULL &&
			    sqlparser_dialect_is_sqlserver_compatible(
				    build->handle->dialect) &&
			    qualifier != NULL &&
			    (sqlparser_text_equal_ci(qualifier, "inserted") ||
			     sqlparser_text_equal_ci(qualifier, "deleted"))) {
				if (build->dml_result_has_target_relation &&
				    sqlparser_graph_span_append_index(
					    build,
					    &target.star_relations,
					    build->dml_result_target_relation_index,
					    out_error) != 0) {
					return -1;
				}
			} else if (sqlparser_graph_add_star_relations(
					   build,
					   resolution_block,
					   qualifier,
					   expr->column_ref,
					   &target.star_relations,
					   out_error) != 0) {
				return -1;
			}
			sqlparser_graph_set_star_target_source_block(build, &target);
			star_target_index = 0U;
			if (sqlparser_graph_add_target(
				    build,
				    &target,
				    &output_identifier,
				    &star_target_index,
				    out_error) != 0) {
				return -1;
			}
			if (build->building_dml_result && target.star_relations.count > 0U) {
				size_t star_index;

				for (star_index = 0U; star_index < target.star_relations.count; star_index++) {
					unsigned int target_reference_kinds;
					int source_reference;
					size_t relation_index;

					relation_index = build->cache->index_pool[target.star_relations.offset + star_index];
					target_reference_kinds =
						SQLPARSER_DIALECT_DML_TARGET_REFERENCE_NONE;
					source_reference = 0;
					if (build->dml_result_has_target_relation &&
					    relation_index ==
						    build->dml_result_target_relation_index) {
						target_reference_kinds =
							build->dml_result_target_reference_kinds;
						if (build->handle != NULL &&
						    sqlparser_dialect_is_sqlserver_compatible(
							    build->handle->dialect) &&
						    target_reference_kinds ==
							    SQLPARSER_DIALECT_DML_TARGET_REFERENCE_NONE &&
						    qualifier != NULL &&
						    sqlparser_text_equal_ci(
							    qualifier, "inserted")) {
							target_reference_kinds =
								SQLPARSER_DIALECT_DML_TARGET_REFERENCE_AFTER;
						} else if (build->handle != NULL &&
							   sqlparser_dialect_is_sqlserver_compatible(
								   build->handle->dialect) &&
							   target_reference_kinds ==
								   SQLPARSER_DIALECT_DML_TARGET_REFERENCE_NONE &&
							   qualifier != NULL &&
							   sqlparser_text_equal_ci(
								   qualifier, "deleted")) {
							target_reference_kinds =
								SQLPARSER_DIALECT_DML_TARGET_REFERENCE_BEFORE;
						}
					} else {
						source_reference = 1;
					}
					if (sqlparser_graph_dml_result_add_resolved_references(
						    build,
						    star_target_index,
						    0U,
						    0,
						    relation_index,
						    target_reference_kinds,
						    source_reference,
						    out_error) != 0) {
						return -1;
					}
				}
			}
			if (out_target_index != NULL) {
				*out_target_index = star_target_index;
			}
			return 0;
		}
		if (sqlparser_graph_column_ref_is_dialect_expression(
			    build,
			    expr->column_ref)) {
			target.kind = SQLPARSER_GRAPH_TARGET_EXPRESSION;
			return sqlparser_graph_add_target(
				build,
				&target,
				&output_identifier,
				out_target_index,
				out_error);
		}
		if (sqlparser_graph_column_ref_is_pseudo(
			    build,
			    expr->column_ref) ||
		    (build->building_dml_result && build->dml_result_action_marker != NULL &&
		     sqlparser_text_equal_ci(name, build->dml_result_action_marker))) {
			int hierarchy_pseudo;

			hierarchy_pseudo =
				sqlparser_graph_column_ref_is_hierarchy_pseudo(
					build,
					expr->column_ref);
			if (build->collect_relation_bindings &&
			    !hierarchy_pseudo &&
			    sqlparser_graph_add_column_ref_field(
				    build,
				    block_index,
				    clause,
				    expr->column_ref,
				    0U,
				    0,
				    NULL,
				    out_error) != 0) {
				return -1;
			}
			target.kind = SQLPARSER_GRAPH_TARGET_PSEUDO;
			if (target.output_name == NULL) {
				target.output_name = build->dml_result_action_marker != NULL &&
					sqlparser_text_equal_ci(name, build->dml_result_action_marker) ?
					"$action" : name;
			}
			target_index = 0U;
			if (sqlparser_graph_add_target(
				    build,
				    &target,
				    &output_identifier,
				    &target_index,
				    out_error) != 0) {
				return -1;
			}
			if (!build->collect_relation_bindings &&
			    hierarchy_pseudo) {
				sqlparser_graph_target_cache_t *cached_target;

				field_index = 0U;
				if (sqlparser_graph_add_column_ref_field(
					    build,
					    block_index,
					    clause,
					    expr->column_ref,
					    target_index,
					    1,
					    &field_index,
					    out_error) != 0) {
					return -1;
				}
				cached_target = sqlparser_graph_target_by_local(
					build,
					target_index);
				if (cached_target != NULL) {
					cached_target->field_index = field_index;
					cached_target->flags |=
						SQLPARSER_GRAPH_TARGET_HAS_FIELD;
				}
			}
			if (build->building_dml_result &&
			    build->dml_result_has_target_relation &&
			    sqlparser_graph_dml_result_add_resolved_references(
				    build,
				    target_index,
				    0U,
				    0,
				    build->dml_result_target_relation_index,
				    build->dml_result_target_reference_kinds,
				    0,
				    out_error) != 0) {
				return -1;
			}
			if (out_target_index != NULL) {
				*out_target_index = target_index;
			}
			return 0;
		}
		target_index = 0U;
		target.kind = SQLPARSER_GRAPH_TARGET_FIELD;
		if (target.output_name == NULL) {
			target.output_name = name;
		}
		if (sqlparser_graph_add_target(
			    build,
			    &target,
			    &output_identifier,
			    &target_index,
			    out_error) != 0) {
			return -1;
		}
		field_index = 0U;
		if (sqlparser_graph_add_column_ref_field(
			    build,
			    block_index,
			    clause,
			    expr->column_ref,
			    target_index,
			    1,
			    &field_index,
			    out_error) != 0) {
			return -1;
		}
		{
			sqlparser_graph_target_cache_t *cached_target;

			cached_target = sqlparser_graph_target_by_local(
				build,
				target_index);
			if (cached_target != NULL) {
				cached_target->field_index = field_index;
				cached_target->flags |=
					SQLPARSER_GRAPH_TARGET_HAS_FIELD;
			}
		}
		if (out_target_index != NULL) {
			*out_target_index = target_index;
		}
		return 0;
	}
	if (expr != NULL && expr->node_case == PG_QUERY__NODE__NODE_A_CONST) {
		target.kind = SQLPARSER_GRAPH_TARGET_LITERAL;
		if (sqlparser_graph_add_target_value_from_node(
			    build,
			    block_index,
			    clause,
			    expr,
			    &target.value_index,
			    NULL,
			    out_error) != 0) {
			return -1;
		}
		target.has_value = 1;
		return sqlparser_graph_add_target(
			build,
			&target,
			&output_identifier,
			out_target_index,
			out_error);
	}
	if (expr != NULL && expr->node_case == PG_QUERY__NODE__NODE_PARAM_REF) {
		int added;

		target.kind = SQLPARSER_GRAPH_TARGET_BIND;
		added = 0;
		if (sqlparser_graph_add_target_value_from_node(
			    build,
			    block_index,
			    clause,
			    expr,
			    &target.value_index,
			    &added,
			    out_error) != 0) {
			return -1;
		}
		if (!added) {
			target.kind = SQLPARSER_GRAPH_TARGET_EXPRESSION;
			return sqlparser_graph_add_target(
				build,
				&target,
				&output_identifier,
				out_target_index,
				out_error);
		}
		target.has_value = 1;
		return sqlparser_graph_add_target(
			build,
			&target,
			&output_identifier,
			out_target_index,
			out_error);
	}
	if (expr != NULL && expr->node_case == PG_QUERY__NODE__NODE_SUB_LINK && expr->sub_link != NULL) {
		target.kind = SQLPARSER_GRAPH_TARGET_SUBQUERY;
		if (sqlparser_graph_build_sublink(build, expr->sub_link, &target.source_block_index, out_error) != 0) {
			return -1;
		}
		target.has_source_block = 1;
		return sqlparser_graph_add_target(
			build,
			&target,
			&output_identifier,
			out_target_index,
			out_error);
	}
	target.kind = SQLPARSER_GRAPH_TARGET_EXPRESSION;
	if (sqlparser_graph_add_target(
		    build,
		    &target,
		    &output_identifier,
		    &target_index,
		    out_error) != 0) {
		return -1;
	}
	if (sqlparser_graph_walk_expr(
		build,
		block_index,
		clause,
		expr,
		target_index,
		1,
		out_error) != 0) {
		return -1;
	}
	if (out_target_index != NULL) {
		*out_target_index = target_index;
	}
	return 0;
}

static int sqlparser_graph_add_using_field(
	sqlparser_graph_build_t *build,
	size_t block_index,
	PgQuery__Node *using_node,
	size_t relation_begin,
	size_t relation_end,
	sqlparser_error_t *out_error)
{
	sqlparser_graph_field_t field;
	sqlparser_identifier_source_t column_source;
	size_t relation_index;
	size_t name_index;

	if (using_node == NULL || using_node->node_case != PG_QUERY__NODE__NODE_STRING ||
	    using_node->string == NULL || using_node->string->sval == NULL) {
		return 0;
	}
	memset(&field, 0, sizeof(field));
	field.block_index = block_index;
	field.clause = SQLPARSER_CLAUSE_KIND_ON;
	field.column_name = using_node->string->sval;
	(void)sqlparser_current_identifier_source(
		build->handle,
		(const char *const *)&using_node->string->sval,
		using_node->string->location,
		0U,
		&column_source);
	field.quoted_identifier =
		column_source.known && column_source.delimited;
	for (relation_index = relation_begin; relation_index < relation_end; relation_index++) {
		sqlparser_graph_relation_t *relation;

		relation = sqlparser_graph_relation_by_local(build, relation_index);
		if (relation != NULL && relation->block_index == block_index &&
		    sqlparser_graph_span_append_index(
			    build,
			    &field.candidate_relations,
			    relation_index,
			    out_error) != 0) {
			return -1;
		}
	}
	name_index = sqlparser_graph_find_cached_name_index(build, &using_node->string->sval);
	if (name_index != (size_t)-1) {
		field.selector.kind = SQLPARSER_SELECTOR_KIND_NAME;
		field.selector.statement_index = build->statement_index;
		field.selector.item_index = name_index;
		field.has_selector = 1;
	}
	return sqlparser_graph_add_field(build, &field, NULL, out_error);
}

static int sqlparser_graph_optional_text_equal(const char *left, const char *right)
{
	left = left != NULL && left[0] != '\0' ? left : NULL;
	right = right != NULL && right[0] != '\0' ? right : NULL;
	return (left == NULL && right == NULL) ||
	       (left != NULL && right != NULL &&
		sqlparser_text_equal_ci(left, right));
}

static int sqlparser_graph_range_var_equal(
	const sqlparser_handle_t *handle,
	const PgQuery__RangeVar *left,
	const PgQuery__RangeVar *right)
{
	sqlparser_relation_view_t left_view;
	sqlparser_relation_view_t right_view;

	if (left == NULL || right == NULL) {
		return 0;
	}
	sqlparser_fill_relation_view_for_handle(handle, left, &left_view);
	sqlparser_fill_relation_view_for_handle(handle, right, &right_view);
	return sqlparser_graph_optional_text_equal(left_view.database_name, right_view.database_name) &&
	       sqlparser_graph_optional_text_equal(left_view.schema_name, right_view.schema_name) &&
	       sqlparser_graph_optional_text_equal(left_view.table_name, right_view.table_name) &&
	       sqlparser_graph_optional_text_equal(left_view.alias_name, right_view.alias_name);
}

static int sqlparser_graph_build_from_item(
	sqlparser_graph_build_t *build,
	size_t block_index,
	PgQuery__Node *node,
	sqlparser_error_t *out_error)
{
	sqlparser_graph_relation_identifier_t derived_identifiers;
	sqlparser_relation_view_t relation_view;
	size_t relation_index;
	size_t source_block_index;

	if (node == NULL) {
		return 0;
	}
	switch (node->node_case) {
		case PG_QUERY__NODE__NODE_RANGE_VAR:
			{
				size_t selector_index;
				size_t added_relation;
				size_t relation_source_block;
				PgQuery__CommonTableExpr *cte;
				sqlparser_graph_relation_identifier_t identifiers;
				sqlparser_graph_relation_kind_t relation_kind;
				sqlparser_identifier_source_t dual_source;

				if (node->range_var == NULL) {
					return 0;
				}
				if (build->skip_duplicate_delete_target &&
				    block_index == build->duplicate_delete_block_index &&
				    sqlparser_graph_range_var_equal(
					    build->handle,
					    build->duplicate_delete_target,
					    node->range_var)) {
					build->skip_duplicate_delete_target = 0;
					return 0;
				}
				sqlparser_fill_relation_view_for_handle(
					build->handle,
					node->range_var,
					&relation_view);
				sqlparser_range_var_identifier_sources(
					build->handle,
					node->range_var,
					&identifiers);
				cte = relation_view.link_name == NULL ?
					sqlparser_graph_find_cte(
						build,
						node->range_var,
						relation_view.table_name) :
					NULL;
				memset(&dual_source, 0, sizeof(dual_source));
				dual_source.known = 1;
				relation_kind = cte != NULL ?
					SQLPARSER_GRAPH_REL_CTE :
					(relation_view.table_name != NULL &&
					 sqlparser_identifier_semantic_equal(
						build->handle,
						relation_view.table_name,
						identifiers.object,
						"dual",
						dual_source) ?
						SQLPARSER_GRAPH_REL_DUAL :
						SQLPARSER_GRAPH_REL_BASE);
				if (sqlparser_graph_add_relation(
					    build,
					    block_index,
					    relation_kind,
					    &relation_view,
					    &identifiers,
					    &added_relation,
					    out_error) != 0) {
					return -1;
				}
				selector_index = sqlparser_graph_find_relation_selector_index(build, node->range_var);
				if (selector_index != (size_t)-1) {
					sqlparser_graph_relation_t *relation;

					relation = sqlparser_graph_relation_by_local(build, added_relation);
					if (relation != NULL) {
						relation->selector.kind = SQLPARSER_SELECTOR_KIND_RELATION;
						relation->selector.statement_index = build->statement_index;
						relation->selector.item_index = selector_index;
						relation->has_selector = 1;
						sqlparser_graph_collect_register_relation(
							build,
							added_relation,
							relation);
					}
				}
				if (cte != NULL) {
					sqlparser_graph_cte_entry_t *cte_entry;
					sqlparser_graph_relation_t *relation;

					relation_source_block = 0U;
					if (sqlparser_graph_ensure_cte_block(
						    build,
						    cte,
						    &relation_source_block,
						    out_error) != 0) {
						return -1;
					}
					cte_entry = sqlparser_graph_find_cte_entry(build, cte);
					if (cte_entry == NULL || !cte_entry->has_block) {
						return 0;
					}
					relation = sqlparser_graph_relation_by_local(build, added_relation);
					if (relation != NULL) {
						relation->source_block_index = relation_source_block;
						relation->has_source_block = 1;
					}
				}
				return 0;
			}
		case PG_QUERY__NODE__NODE_RANGE_SUBSELECT:
			if (node->range_subselect == NULL ||
			    node->range_subselect->subquery == NULL ||
			    node->range_subselect->subquery->node_case != PG_QUERY__NODE__NODE_SELECT_STMT) {
				return 0;
			}
			memset(&relation_view, 0, sizeof(relation_view));
			relation_view.alias_name =
				node->range_subselect->alias != NULL &&
					node->range_subselect->alias->aliasname != NULL &&
					node->range_subselect->alias->aliasname[0] != '\0'
					? node->range_subselect->alias->aliasname
					: NULL;
			memset(
				&derived_identifiers,
				0,
				sizeof(derived_identifiers));
			if (relation_view.alias_name != NULL) {
				(void)sqlparser_identifier_component_source(
					build->handle,
					node->range_subselect->alias->location,
					0U,
					&derived_identifiers.alias,
					NULL);
			}
			if (sqlparser_graph_add_relation(
				    build,
				    block_index,
				    SQLPARSER_GRAPH_REL_DERIVED,
				    &relation_view,
				    &derived_identifiers,
				    &relation_index,
				    out_error) != 0 ||
			    sqlparser_graph_build_select(
				    build,
				    node->range_subselect->subquery->select_stmt,
				    SQLPARSER_GRAPH_BLOCK_SELECT,
				    &source_block_index,
				    out_error) != 0) {
				return -1;
			}
			sqlparser_graph_relation_by_local(build, relation_index)->source_block_index = source_block_index;
			sqlparser_graph_relation_by_local(build, relation_index)->has_source_block = 1;
			return 0;
		case PG_QUERY__NODE__NODE_RANGE_FUNCTION:
			return node->range_function != NULL ?
				sqlparser_graph_walk_node_array(
					build,
					block_index,
					SQLPARSER_CLAUSE_KIND_UNKNOWN,
					node->range_function->functions,
					node->range_function->n_functions,
					out_error) :
				0;
		case PG_QUERY__NODE__NODE_RANGE_TABLE_FUNC:
		case PG_QUERY__NODE__NODE_JSON_TABLE:
			return sqlparser_graph_walk_expr(
				build,
				block_index,
				SQLPARSER_CLAUSE_KIND_UNKNOWN,
				node,
				0U,
				0,
				out_error);
		case PG_QUERY__NODE__NODE_RANGE_TABLE_SAMPLE:
			if (node->range_table_sample == NULL) {
				return 0;
			}
			if (sqlparser_graph_build_from_item(
				    build,
				    block_index,
				    node->range_table_sample->relation,
				    out_error) != 0 ||
			    sqlparser_graph_walk_node_array(
				    build,
				    block_index,
				    SQLPARSER_CLAUSE_KIND_UNKNOWN,
				    node->range_table_sample->args,
				    node->range_table_sample->n_args,
				    out_error) != 0) {
				return -1;
			}
			return sqlparser_graph_walk_expr(
				build,
				block_index,
				SQLPARSER_CLAUSE_KIND_UNKNOWN,
				node->range_table_sample->repeatable,
				0U,
				0,
				out_error);
		case PG_QUERY__NODE__NODE_JOIN_EXPR:
			if (node->join_expr == NULL) {
				return 0;
			}
			{
				size_t relation_begin;
				size_t relation_end;
				size_t using_index;

				relation_begin = sqlparser_graph_local_relation_count(build);
				if (sqlparser_graph_build_from_item(build, block_index, node->join_expr->larg, out_error) != 0 ||
				    sqlparser_graph_build_from_item(build, block_index, node->join_expr->rarg, out_error) != 0) {
					return -1;
				}
				relation_end = sqlparser_graph_local_relation_count(build);
				for (using_index = 0U; using_index < node->join_expr->n_using_clause; using_index++) {
					if (sqlparser_graph_add_using_field(
						    build,
						    block_index,
						    node->join_expr->using_clause[using_index],
						    relation_begin,
						    relation_end,
						    out_error) != 0) {
						return -1;
					}
				}
				return sqlparser_graph_walk_predicate_expr(
					build,
					block_index,
					SQLPARSER_CLAUSE_KIND_ON,
					node->join_expr->quals,
					out_error);
			}
		default:
			return 0;
	}
}

static sqlparser_graph_set_kind_t sqlparser_graph_set_kind_from_select(PgQuery__SelectStmt *stmt)
{
	if (stmt == NULL) {
		return SQLPARSER_GRAPH_SET_UNION;
	}
	switch (stmt->op) {
		case PG_QUERY__SET_OPERATION__SETOP_INTERSECT:
			return SQLPARSER_GRAPH_SET_INTERSECT;
		case PG_QUERY__SET_OPERATION__SETOP_EXCEPT:
			return SQLPARSER_GRAPH_SET_EXCEPT;
		case PG_QUERY__SET_OPERATION__SETOP_UNION:
		default:
			return stmt->all ? SQLPARSER_GRAPH_SET_UNION_ALL : SQLPARSER_GRAPH_SET_UNION;
	}
}

static int sqlparser_graph_build_select_impl(
	sqlparser_graph_build_t *build,
	PgQuery__SelectStmt *stmt,
	sqlparser_graph_block_kind_t kind,
	size_t *out_block_index,
	sqlparser_error_t *out_error)
{
	sqlparser_graph_scope_t scope;
	size_t block_index;
	size_t connect_predicate_index;
	size_t index;
	size_t target_list_index;

	if (out_block_index != NULL) {
		*out_block_index = 0U;
	}
	if (stmt == NULL) {
		return 0;
	}
	if (stmt->op != PG_QUERY__SET_OPERATION__SET_OPERATION_UNDEFINED &&
	    stmt->op != PG_QUERY__SET_OPERATION__SETOP_NONE &&
	    (stmt->larg != NULL || stmt->rarg != NULL)) {
		sqlparser_graph_set_t set_item;
		int has_scope;
		size_t left_block;
		size_t right_block;

		has_scope = stmt->with_clause != NULL;
		left_block = 0U;
		right_block = 0U;
		if (sqlparser_graph_add_block(build, SQLPARSER_GRAPH_BLOCK_SET, &block_index, out_error) != 0) {
			return -1;
		}
		sqlparser_graph_register_cte_block(build, stmt, block_index);
		if (out_block_index != NULL) {
			*out_block_index = block_index;
		}
		if (has_scope) {
			sqlparser_graph_push_scope(
				build,
				&scope,
				block_index,
				stmt->with_clause);
		}
		memset(&set_item, 0, sizeof(set_item));
		set_item.kind = sqlparser_graph_set_kind_from_select(stmt);
		set_item.result_block_index = block_index;
		if (stmt->larg != NULL &&
		    sqlparser_graph_build_select(build, stmt->larg, SQLPARSER_GRAPH_BLOCK_SELECT, &left_block, out_error) != 0) {
			goto set_fail;
		}
		if (stmt->rarg != NULL &&
		    sqlparser_graph_build_select(build, stmt->rarg, SQLPARSER_GRAPH_BLOCK_SELECT, &right_block, out_error) != 0) {
			goto set_fail;
		}
		if (stmt->larg != NULL &&
		    sqlparser_graph_span_append_index(build, &set_item.branch_blocks, left_block, out_error) != 0) {
			goto set_fail;
		}
		if (stmt->rarg != NULL &&
		    sqlparser_graph_span_append_index(build, &set_item.branch_blocks, right_block, out_error) != 0) {
			goto set_fail;
		}
		if (sqlparser_graph_walk_node_array(build, block_index, SQLPARSER_CLAUSE_KIND_ORDER_BY, stmt->sort_clause, stmt->n_sort_clause, out_error) != 0 ||
		    sqlparser_graph_walk_expr(build, block_index, SQLPARSER_CLAUSE_KIND_UNKNOWN, stmt->limit_offset, 0U, 0, out_error) != 0 ||
		    sqlparser_graph_walk_expr(build, block_index, SQLPARSER_CLAUSE_KIND_UNKNOWN, stmt->limit_count, 0U, 0, out_error) != 0 ||
		    sqlparser_graph_ensure_ctes(build, stmt->with_clause, out_error) != 0 ||
		    sqlparser_graph_add_set(build, &set_item, NULL, out_error) != 0) {
			goto set_fail;
		}
		if (has_scope) {
			sqlparser_graph_pop_scope(build);
		}
		return 0;

	set_fail:
		if (has_scope) {
			sqlparser_graph_pop_scope(build);
		}
		return -1;
	}
	if (sqlparser_graph_add_block(build, kind, &block_index, out_error) != 0) {
		return -1;
	}
	sqlparser_graph_register_cte_block(build, stmt, block_index);
	if (out_block_index != NULL) {
		*out_block_index = block_index;
	}
	sqlparser_graph_push_scope(
		build,
		&scope,
		block_index,
		stmt->with_clause);
	for (index = 0U; index < stmt->n_from_clause; index++) {
		if (sqlparser_graph_build_from_item(build, block_index, stmt->from_clause[index], out_error) != 0) {
			sqlparser_graph_pop_scope(build);
			return -1;
		}
	}
	if (sqlparser_graph_walk_node_array(
		    build,
		    block_index,
		    SQLPARSER_CLAUSE_KIND_SELECT_LIST,
		    stmt->values_lists,
		    stmt->n_values_lists,
		    out_error) != 0) {
		sqlparser_graph_pop_scope(build);
		return -1;
	}
	if (build->collect_relation_bindings &&
	    sqlparser_graph_walk_node_array(
		    build,
		    block_index,
		    SQLPARSER_CLAUSE_KIND_SELECT_LIST,
		    stmt->distinct_clause,
		    stmt->n_distinct_clause,
		    out_error) != 0) {
		sqlparser_graph_pop_scope(build);
		return -1;
	}
	target_list_index = 0U;
	index = sqlparser_graph_find_cached_select_target_list_index(build, stmt);
	if (index != (size_t)-1) {
		target_list_index = index;
	}
	for (index = 0U; index < stmt->n_target_list; index++) {
		PgQuery__Node *target_node;

		target_node = stmt->target_list[index];
		if (target_node != NULL &&
		    target_node->node_case == PG_QUERY__NODE__NODE_RES_TARGET &&
		    sqlparser_graph_build_target(
			    build,
			    block_index,
			    SQLPARSER_SELECTOR_KIND_SELECT_TARGETS,
			    SQLPARSER_SELECTOR_KIND_SELECT_TARGET,
			    target_list_index,
			    0U,
			    index,
			    SQLPARSER_CLAUSE_KIND_SELECT_LIST,
			    target_node->res_target,
			    NULL,
			    out_error) != 0) {
			sqlparser_graph_pop_scope(build);
			return -1;
		}
	}
	if (sqlparser_graph_walk_predicate_expr(
		    build,
		    block_index,
		    SQLPARSER_CLAUSE_KIND_WHERE,
		    stmt->where_clause,
		    out_error) != 0 ||
	    sqlparser_graph_walk_predicate_expr(
		    build,
		    block_index,
		    SQLPARSER_CLAUSE_KIND_START_WITH,
		    stmt->start_with_clause,
		    out_error) != 0) {
		sqlparser_graph_pop_scope(build);
		return -1;
	}
	connect_predicate_index = sqlparser_graph_local_predicate_count(build);
	if (sqlparser_graph_walk_predicate_expr(
		    build,
		    block_index,
		    SQLPARSER_CLAUSE_KIND_CONNECT_BY,
		    stmt->connect_by_clause,
		    out_error) != 0) {
		sqlparser_graph_pop_scope(build);
		return -1;
	}
	if (!build->collect_relation_bindings &&
	    stmt->connect_by_no_cycle &&
	    connect_predicate_index < sqlparser_graph_local_predicate_count(build)) {
		build->cache->predicates[
			build->statement->predicate_offset +
			connect_predicate_index].nocycle = 1;
	}
	if (sqlparser_graph_walk_node_array(
		    build,
		    block_index,
		    SQLPARSER_CLAUSE_KIND_GROUP_BY,
		    stmt->group_clause,
		    stmt->n_group_clause,
		    out_error) != 0 ||
	    sqlparser_graph_walk_predicate_expr(
		    build,
		    block_index,
		    SQLPARSER_CLAUSE_KIND_HAVING,
		    stmt->having_clause,
		    out_error) != 0) {
		sqlparser_graph_pop_scope(build);
		return -1;
	}
	for (index = 0U; index < stmt->n_window_clause; index++) {
		PgQuery__Node *window_node;
		PgQuery__WindowDef *window;

		window_node = stmt->window_clause[index];
		window = window_node != NULL &&
			window_node->node_case == PG_QUERY__NODE__NODE_WINDOW_DEF ?
			window_node->window_def : NULL;
		if (window != NULL &&
		    (sqlparser_graph_walk_node_array(
			     build,
			     block_index,
			     SQLPARSER_CLAUSE_KIND_WINDOW_PARTITION,
			     window->partition_clause,
			     window->n_partition_clause,
			     out_error) != 0 ||
		     sqlparser_graph_walk_node_array(
			     build,
			     block_index,
			     SQLPARSER_CLAUSE_KIND_ORDER_BY,
			     window->order_clause,
			     window->n_order_clause,
			     out_error) != 0 ||
		     sqlparser_graph_walk_expr(
			     build,
			     block_index,
			     SQLPARSER_CLAUSE_KIND_UNKNOWN,
			     window->start_offset,
			     0U,
			     0,
			     out_error) != 0 ||
		     sqlparser_graph_walk_expr(
			     build,
			     block_index,
			     SQLPARSER_CLAUSE_KIND_UNKNOWN,
			     window->end_offset,
			     0U,
			     0,
			     out_error) != 0)) {
			sqlparser_graph_pop_scope(build);
			return -1;
		}
	}
	if (sqlparser_graph_walk_node_array(
		    build,
		    block_index,
		    SQLPARSER_CLAUSE_KIND_ORDER_BY,
		    stmt->sort_clause,
		    stmt->n_sort_clause,
		    out_error) != 0) {
		sqlparser_graph_pop_scope(build);
		return -1;
	}
	if (sqlparser_graph_walk_expr(
		    build,
		    block_index,
		    SQLPARSER_CLAUSE_KIND_UNKNOWN,
		    stmt->limit_offset,
		    0U,
		    0,
		    out_error) != 0 ||
	    sqlparser_graph_walk_expr(
		    build,
		    block_index,
		    SQLPARSER_CLAUSE_KIND_UNKNOWN,
		    stmt->limit_count,
		    0U,
		    0,
		    out_error) != 0 ||
	    sqlparser_graph_ensure_ctes(build, stmt->with_clause, out_error) != 0) {
		sqlparser_graph_pop_scope(build);
		return -1;
	}
	sqlparser_graph_pop_scope(build);
	return 0;
}

static int sqlparser_graph_build_select(
	sqlparser_graph_build_t *build,
	PgQuery__SelectStmt *stmt,
	sqlparser_graph_block_kind_t kind,
	size_t *out_block_index,
	sqlparser_error_t *out_error)
{
	sqlparser_graph_dml_result_build_state_t saved;
	sqlparser_target_path_entry_t saved_target_path[
		SQLPARSER_TARGET_PATH_CAPACITY];
	size_t saved_target_path_count;
	unsigned int saved_hierarchy_prior_depth;
	int saved_hierarchy_query_active;
	int suspended;
	int status;

	saved_target_path_count = build != NULL ? build->target_path_count : 0U;
	saved_hierarchy_query_active = build != NULL ?
		build->hierarchy_query_active : 0;
	saved_hierarchy_prior_depth = build != NULL ?
		build->hierarchy_prior_depth : 0U;
	if (saved_target_path_count > SQLPARSER_TARGET_PATH_CAPACITY) {
		saved_target_path_count = SQLPARSER_TARGET_PATH_CAPACITY;
	}
	if (saved_target_path_count > 0U) {
		memcpy(
			saved_target_path,
			build->target_path,
			saved_target_path_count * sizeof(saved_target_path[0]));
	}
	suspended = build != NULL && build->building_dml_result;
	if (suspended) {
		sqlparser_graph_dml_result_build_state_suspend(build, &saved);
	}
	if (build != NULL) {
		build->target_path_count = 0U;
		build->hierarchy_query_active =
			stmt != NULL && stmt->connect_by_clause != NULL;
		build->hierarchy_prior_depth = 0U;
	}
	status = sqlparser_graph_build_select_impl(
		build, stmt, kind, out_block_index, out_error);
	if (saved_target_path_count > 0U) {
		memcpy(
			build->target_path,
			saved_target_path,
			saved_target_path_count * sizeof(saved_target_path[0]));
	}
	if (build != NULL) {
		build->target_path_count = saved_target_path_count;
		build->hierarchy_query_active = saved_hierarchy_query_active;
		build->hierarchy_prior_depth = saved_hierarchy_prior_depth;
	}
	if (suspended) {
		sqlparser_graph_dml_result_build_state_restore(build, &saved);
	}
	return status;
}

static int sqlparser_graph_finalize_statement_spans(sqlparser_graph_build_t *build, sqlparser_error_t *out_error)
{
	size_t block_index;
	size_t dml_index;

	if (build == NULL || build->statement == NULL) {
		return -1;
	}
	if (build->has_claiming_dml_index) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "PostgreSQL DML inventory claim was not consumed");
		return -1;
	}
	for (dml_index = 0U;
	     dml_index < build->dml_inventory_count;
	     dml_index++) {
		if (!build->dml_inventory_built[dml_index]) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "PostgreSQL DML inventory entry was not built");
			return -1;
		}
	}
	build->statement->block_count = build->cache->block_count - build->statement->block_offset;
	build->statement->relation_count = build->cache->relation_count - build->statement->relation_offset;
	build->statement->target_count = build->cache->target_count - build->statement->target_offset;
	build->statement->field_count = build->cache->field_count - build->statement->field_offset;
	build->statement->value_count = build->cache->value_count - build->statement->value_offset;
	build->statement->set_count = build->cache->set_count - build->statement->set_offset;
	build->statement->predicate_count = build->cache->predicate_count - build->statement->predicate_offset;
	build->statement->dml_count = build->cache->dml_count - build->statement->dml_offset;
	build->statement->dml_branch_count = build->cache->dml_branch_count - build->statement->dml_branch_offset;
	build->statement->dml_column_count = build->cache->dml_column_count - build->statement->dml_column_offset;
	build->statement->dml_cell_count = build->cache->dml_cell_count - build->statement->dml_cell_offset;
	build->statement->dml_assignment_count = build->cache->dml_assignment_count - build->statement->dml_assignment_offset;
	build->statement->dml_reference_count = build->cache->dml_results != NULL ?
		build->cache->dml_results->reference_count - build->statement->dml_reference_offset : 0U;
	build->statement->session_item_count =
		build->cache->session_item_count - build->statement->session_item_offset;
	build->statement->session_value_count =
		build->cache->session_value_count - build->statement->session_value_offset;
	if (build->statement->session_action == SQLPARSER_GRAPH_SESSION_ACTION_UNKNOWN &&
	    (build->statement->session_item_count != 0U ||
	     build->statement->session_value_count != 0U)) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "session graph action is missing");
		return -1;
	}
	for (block_index = 0U; block_index < build->statement->block_count; block_index++) {
		sqlparser_graph_block_t *block;
		size_t index;

		block = sqlparser_graph_block_by_local(build, block_index);
		if (block == NULL) {
			continue;
		}
		memset(&block->relations, 0, sizeof(block->relations));
		for (index = 0U; index < build->statement->relation_count; index++) {
			sqlparser_graph_relation_t *relation;

			relation = sqlparser_graph_relation_by_local(build, index);
			if (relation != NULL &&
			    relation->block_index == block_index &&
			    sqlparser_graph_span_append_index(build, &block->relations, index, out_error) != 0) {
				return -1;
			}
		}
		memset(&block->targets, 0, sizeof(block->targets));
		for (index = 0U; index < build->statement->target_count; index++) {
			sqlparser_graph_target_cache_t *target;

			target = sqlparser_graph_target_by_local(build, index);
			if (target != NULL &&
			    target->block_index == block_index &&
			    sqlparser_graph_span_append_index(build, &block->targets, index, out_error) != 0) {
				return -1;
			}
		}
		memset(&block->predicates, 0, sizeof(block->predicates));
		for (index = 0U; index < build->statement->predicate_count; index++) {
			sqlparser_graph_predicate_t *predicate;

			predicate = &build->cache->predicates[build->statement->predicate_offset + index];
			if (predicate != NULL &&
			    predicate->block_index == block_index &&
			    sqlparser_graph_span_append_index(build, &block->predicates, index, out_error) != 0) {
				return -1;
			}
		}
	}
	return 0;
}

static int sqlparser_graph_add_dml(
	sqlparser_graph_build_t *build,
	const sqlparser_graph_dml_t *source,
	ProtobufCMessage *message,
	size_t *out_index,
	sqlparser_error_t *out_error)
{
	sqlparser_graph_dml_t *dml;
	const sqlparser_dialect_dml_result_dml_t *inventory_dml;
	size_t global_index;
	size_t local_index;

	if (out_index != NULL) {
		*out_index = 0U;
	}
	if (build->dml_inventory_count > 0U) {
		if (!build->has_claiming_dml_index ||
		    build->claiming_dml_index >= build->dml_inventory_count ||
		    build->dml_inventory_built[build->claiming_dml_index]) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "PostgreSQL DML inventory slot is missing or already built");
			return -1;
		}
		local_index = build->claiming_dml_index;
		inventory_dml = sqlparser_graph_dml_inventory_at(
			build, local_index);
		if (inventory_dml == NULL ||
		    inventory_dml->message != message || source == NULL ||
		    inventory_dml->kind != source->kind) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "PostgreSQL DML inventory does not match the statement AST");
			return -1;
		}
		global_index = build->statement->dml_offset + local_index;
		dml = &build->cache->dml[global_index];
		memset(dml, 0, sizeof(*dml));
		*dml = *source;
		dml->index = local_index;
		dml->statement_index = build->statement_index;
		build->dml_inventory_built[local_index] = 1U;
		build->has_claiming_dml_index = 0;
		if (out_index != NULL) {
			*out_index = local_index;
		}
		return 0;
	}
	if (sqlparser_query_graph_reserve_array(
		    (void **)&build->cache->dml,
		    &build->cache->dml_capacity,
		    build->cache->dml_count + 1U,
		    sizeof(*build->cache->dml),
		    out_error) != 0) {
		return -1;
	}
	global_index = build->cache->dml_count++;
	local_index = build->cache->dml_count - build->statement->dml_offset - 1U;
	dml = &build->cache->dml[global_index];
	memset(dml, 0, sizeof(*dml));
	if (source != NULL) {
		*dml = *source;
	}
	dml->index = local_index;
	dml->statement_index = build->statement_index;
	if (out_index != NULL) {
		*out_index = local_index;
	}
	return 0;
}

static int sqlparser_graph_add_dml_column(
	sqlparser_graph_build_t *build,
	const sqlparser_graph_dml_column_t *source,
	size_t *out_index,
	sqlparser_error_t *out_error)
{
	sqlparser_graph_dml_column_t *column;
	size_t global_index;
	size_t local_index;

	if (out_index != NULL) {
		*out_index = 0U;
	}
	if (sqlparser_query_graph_reserve_array(
		    (void **)&build->cache->dml_columns,
		    &build->cache->dml_column_capacity,
		    build->cache->dml_column_count + 1U,
		    sizeof(*build->cache->dml_columns),
		    out_error) != 0) {
		return -1;
	}
	global_index = build->cache->dml_column_count++;
	local_index = build->cache->dml_column_count - build->statement->dml_column_offset - 1U;
	column = &build->cache->dml_columns[global_index];
	memset(column, 0, sizeof(*column));
	if (source != NULL) {
		*column = *source;
	}
	column->index = local_index;
	column->statement_index = build->statement_index;
	if (out_index != NULL) {
		*out_index = local_index;
	}
	return 0;
}

static int sqlparser_graph_add_dml_branch(
	sqlparser_graph_build_t *build,
	const sqlparser_graph_dml_branch_t *source,
	const sqlparser_graph_merge_branch_detail_t *detail_source,
	size_t *out_index,
	sqlparser_error_t *out_error)
{
	sqlparser_graph_dml_branch_t *branch;
	sqlparser_graph_merge_branch_detail_t *detail;
	size_t global_index;
	size_t local_index;

	if (out_index != NULL) {
		*out_index = 0U;
	}
	if (sqlparser_query_graph_reserve_array(
		    (void **)&build->cache->dml_branches,
		    &build->cache->dml_branch_capacity,
		    build->cache->dml_branch_count + 1U,
		    sizeof(*build->cache->dml_branches),
		    out_error) != 0 ||
	    sqlparser_query_graph_reserve_array(
		    (void **)&build->cache->merge_branch_details,
		    &build->cache->merge_branch_detail_capacity,
		    build->cache->dml_branch_count + 1U,
		    sizeof(*build->cache->merge_branch_details),
		    out_error) != 0) {
		return -1;
	}
	global_index = build->cache->dml_branch_count++;
	local_index = build->cache->dml_branch_count - build->statement->dml_branch_offset - 1U;
	branch = &build->cache->dml_branches[global_index];
	detail = &build->cache->merge_branch_details[global_index];
	memset(branch, 0, sizeof(*branch));
	memset(detail, 0, sizeof(*detail));
	if (source != NULL) {
		*branch = *source;
	}
	if (detail_source != NULL) {
		*detail = *detail_source;
	}
	branch->index = local_index;
	branch->statement_index = build->statement_index;
	if (out_index != NULL) {
		*out_index = local_index;
	}
	return 0;
}

static int sqlparser_graph_add_dml_cell(
	sqlparser_graph_build_t *build,
	const sqlparser_graph_dml_cell_t *source,
	const char *expression_sql,
	size_t *out_index,
	sqlparser_error_t *out_error)
{
	sqlparser_graph_dml_cell_cache_t cell;
	size_t global_index;
	size_t local_index;

	if (out_index != NULL) {
		*out_index = 0U;
	}
	if (build == NULL || build->cache == NULL || build->statement == NULL ||
	    source == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "query graph builder or DML cell source is missing");
		return -1;
	}
	if (sqlparser_query_graph_reserve_array_with_initial(
		    (void **)&build->cache->dml_cells,
		    &build->cache->dml_cell_capacity,
		    build->cache->dml_cell_count + 1U,
		    sizeof(*build->cache->dml_cells),
		    4U,
		    out_error) != 0) {
		return -1;
	}
	if (sqlparser_graph_dml_cell_cache_init(
		    build->cache,
		    &cell,
		    source,
		    expression_sql,
		    build->statement_index,
		    out_error) != 0) {
		return -1;
	}
	global_index = build->cache->dml_cell_count;
	local_index = global_index - build->statement->dml_cell_offset;
	build->cache->dml_cells[global_index] = cell;
	build->cache->dml_cell_count++;
	if (out_index != NULL) {
		*out_index = local_index;
	}
	return 0;
}

static int sqlparser_graph_add_dml_assignment(
	sqlparser_graph_build_t *build,
	const sqlparser_graph_dml_assignment_t *source,
	size_t *out_index,
	sqlparser_error_t *out_error)
{
	sqlparser_graph_dml_assignment_t *assignment;
	size_t global_index;
	size_t local_index;

	if (out_index != NULL) {
		*out_index = 0U;
	}
	if (sqlparser_query_graph_reserve_array(
		    (void **)&build->cache->dml_assignments,
		    &build->cache->dml_assignment_capacity,
		    build->cache->dml_assignment_count + 1U,
		    sizeof(*build->cache->dml_assignments),
		    out_error) != 0) {
		return -1;
	}
	global_index = build->cache->dml_assignment_count++;
	local_index = build->cache->dml_assignment_count - build->statement->dml_assignment_offset - 1U;
	assignment = &build->cache->dml_assignments[global_index];
	memset(assignment, 0, sizeof(*assignment));
	if (source != NULL) {
		*assignment = *source;
	}
	assignment->index = local_index;
	assignment->statement_index = build->statement_index;
	if (out_index != NULL) {
		*out_index = local_index;
	}
	return 0;
}

static int sqlparser_graph_fill_dml_value_fields(
	sqlparser_graph_build_t *build,
	PgQuery__Node *value_node,
	sqlparser_graph_value_kind_t *out_kind,
	sqlparser_literal_view_t *out_literal,
	char *bind,
	size_t bind_size,
	int *out_has_bind,
	sqlparser_bind_kind_t *out_bind_kind,
	char *bind_sql,
	size_t bind_sql_size,
	int *out_has_bind_sql,
	size_t *out_bind_position,
	int *out_has_bind_position,
	sqlparser_selector_t *out_selector,
	int *out_has_selector,
	sqlparser_error_t *out_error)
{
	sqlparser_graph_value_t value;
	int value_status;

	if (out_kind != NULL) {
		*out_kind = SQLPARSER_GRAPH_VALUE_EXPRESSION;
	}
	if (out_literal != NULL) {
		sqlparser_literal_view_clear(out_literal);
	}
	if (bind != NULL && bind_size > 0U) {
		bind[0] = '\0';
	}
	if (out_has_bind != NULL) {
		*out_has_bind = 0;
	}
	if (out_bind_kind != NULL) {
		*out_bind_kind = SQLPARSER_BIND_KIND_NONE;
	}
	if (bind_sql != NULL && bind_sql_size > 0U) {
		bind_sql[0] = '\0';
	}
	if (out_has_bind_sql != NULL) {
		*out_has_bind_sql = 0;
	}
	if (out_bind_position != NULL) {
		*out_bind_position = 0U;
	}
	if (out_has_bind_position != NULL) {
		*out_has_bind_position = 0;
	}
	if (out_selector != NULL) {
		memset(out_selector, 0, sizeof(*out_selector));
		out_selector->kind = SQLPARSER_SELECTOR_KIND_UNKNOWN;
	}
	if (out_has_selector != NULL) {
		*out_has_selector = 0;
	}

	memset(&value, 0, sizeof(value));
	value_status = sqlparser_graph_value_from_node(
		build,
		0U,
		SQLPARSER_CLAUSE_KIND_UNKNOWN,
		NULL,
		0U,
		0,
		SQLPARSER_GRAPH_FIELD_MATCH_UNKNOWN,
		value_node,
		NULL,
		&value,
		out_error);
	if (value_status < 0) {
		return -1;
	}
	if (value_status == 0) {
		if (out_kind != NULL) {
			*out_kind = SQLPARSER_GRAPH_VALUE_EXPRESSION;
		}
		return 0;
	}
	if (out_kind != NULL) {
		*out_kind = value.kind;
	}
	if (out_literal != NULL) {
		*out_literal = value.literal;
	}
	if (bind != NULL) {
		sqlparser_view_copy_public_text(bind, bind_size, value.has_bind ? value.bind : NULL, NULL);
		if (out_has_bind != NULL) {
			*out_has_bind = bind[0] != '\0';
		}
	}
	if (out_bind_kind != NULL) {
		*out_bind_kind = value.bind_kind;
	}
	if (bind_sql != NULL) {
		sqlparser_view_copy_public_text(bind_sql, bind_sql_size, value.has_bind_sql ? value.bind_sql : NULL, NULL);
		if (out_has_bind_sql != NULL) {
			*out_has_bind_sql = bind_sql[0] != '\0';
		}
	}
	if (out_bind_position != NULL) {
		*out_bind_position = value.bind_position;
	}
	if (out_has_bind_position != NULL) {
		*out_has_bind_position = value.has_bind_position;
	}
	if (out_selector != NULL) {
		*out_selector = value.selector;
	}
	if (out_has_selector != NULL) {
		*out_has_selector = value.has_selector;
	}
	return 0;
}

static int sqlparser_graph_resolve_source_target_from_field(
	sqlparser_graph_build_t *build,
	size_t field_index,
	PgQuery__ColumnRef *column_ref,
	size_t *out_source_target_index)
{
	sqlparser_identifier_source_t field_identifier;
	sqlparser_graph_field_t *field;
	sqlparser_graph_relation_t *relation;
	size_t index;
	size_t match_index;
	size_t match_count;

	if (out_source_target_index != NULL) {
		*out_source_target_index = 0U;
	}
	field = sqlparser_graph_field_by_local(build, field_index);
	if (build == NULL || field == NULL || !field->has_relation || field->column_name == NULL) {
		return 0;
	}
	relation = sqlparser_graph_relation_by_local(build, field->relation_index);
	if (relation == NULL || !relation->has_source_block) {
		return 0;
	}
	field_identifier = sqlparser_graph_column_ref_part_source(
		build->handle,
		column_ref,
		0U);
	match_index = 0U;
	match_count = 0U;
	for (index = 0U; index < sqlparser_graph_local_target_count(build); index++) {
		sqlparser_identifier_source_t *target_identifier;
		sqlparser_graph_target_cache_t *target;

		target = sqlparser_graph_target_by_local(build, index);
		target_identifier =
			sqlparser_graph_target_identifier_by_local(build, index);
		if (target == NULL ||
		    target->block_index != relation->source_block_index ||
		    target->output_name == NULL ||
		    target_identifier == NULL ||
		    !sqlparser_identifier_semantic_equal(
			    build->handle,
			    target->output_name,
			    *target_identifier,
			    field->column_name,
			    field_identifier)) {
			continue;
		}
		match_index = index;
		match_count++;
		if (match_count > 1U) {
			return 0;
		}
	}
	if (match_count == 1U && out_source_target_index != NULL) {
		*out_source_target_index = match_index;
	}
	return match_count == 1U;
}

static int sqlparser_graph_resolve_source_target_from_block(
	sqlparser_graph_build_t *build,
	size_t block_index,
	size_t ordinal,
	size_t *out_source_target_index)
{
	size_t index;
	size_t match_index;
	size_t match_count;

	if (out_source_target_index != NULL) {
		*out_source_target_index = 0U;
	}
	match_index = 0U;
	match_count = 0U;
	for (index = 0U;
	     index < sqlparser_graph_local_target_count(build);
	     index++) {
		sqlparser_graph_target_cache_t *target;

		target = sqlparser_graph_target_by_local(build, index);
		if (target == NULL || target->block_index != block_index ||
		    target->ordinal != ordinal) {
			continue;
		}
		match_index = index;
		match_count++;
		if (match_count > 1U) {
			return 0;
		}
	}
	if (match_count == 1U && out_source_target_index != NULL) {
		*out_source_target_index = match_index;
	}
	return match_count == 1U;
}

static int sqlparser_graph_fill_dml_source_field(
	sqlparser_graph_build_t *build,
	size_t block_index,
	sqlparser_clause_kind_t clause,
	PgQuery__Node *value_node,
	sqlparser_graph_value_kind_t *kind,
	size_t *source_field_index,
	int *has_source_field,
	size_t *source_target_index,
	int *has_source_target,
	sqlparser_error_t *out_error)
{
	size_t field_index;

	if (source_field_index != NULL) {
		*source_field_index = 0U;
	}
	if (has_source_field != NULL) {
		*has_source_field = 0;
	}
	if (source_target_index != NULL) {
		*source_target_index = 0U;
	}
	if (has_source_target != NULL) {
		*has_source_target = 0;
	}
	value_node = sqlparser_unwrap_grouping_node(value_node);
	if (value_node == NULL ||
	    value_node->node_case != PG_QUERY__NODE__NODE_COLUMN_REF ||
	    value_node->column_ref == NULL ||
	    !sqlparser_graph_column_ref_is_recordable_field(
		    build,
		    value_node->column_ref)) {
		return 0;
	}
	field_index = 0U;
	if (sqlparser_graph_add_column_ref_field(
		    build,
		    block_index,
		    clause,
		    value_node->column_ref,
		    0U,
		    0,
		    &field_index,
		    out_error) != 0) {
		return -1;
	}
	if (kind != NULL) {
		*kind = SQLPARSER_GRAPH_VALUE_FIELD;
	}
	if (source_field_index != NULL) {
		*source_field_index = field_index;
	}
	if (has_source_field != NULL) {
		*has_source_field = 1;
	}
	if (source_target_index != NULL &&
	    sqlparser_graph_resolve_source_target_from_field(
		    build,
		    field_index,
		    value_node->column_ref,
		    source_target_index)) {
		if (has_source_target != NULL) {
			*has_source_target = 1;
		}
	}
	return 1;
}

static int sqlparser_graph_add_dml_target_relation(
	sqlparser_graph_build_t *build,
	PgQuery__RangeVar *range_var,
	size_t block_index,
	size_t *out_relation_index,
	sqlparser_error_t *out_error)
{
	PgQuery__CommonTableExpr *cte;
	sqlparser_graph_relation_identifier_t identifiers;
	sqlparser_relation_view_t relation_view;
	sqlparser_graph_relation_t *relation;
	size_t relation_index;
	size_t source_block_index;
	size_t selector_index;
	int rc;

	if (out_relation_index != NULL) {
		*out_relation_index = 0U;
	}
	if (range_var == NULL) {
		return 0;
	}
	sqlparser_fill_relation_view_for_handle(build->handle, range_var, &relation_view);
	sqlparser_range_var_identifier_sources(
		build->handle,
		range_var,
		&identifiers);
	cte = relation_view.link_name == NULL &&
		build->handle != NULL &&
		sqlparser_dialect_is_sqlserver_compatible(build->handle->dialect) ?
		sqlparser_graph_find_cte(
			build,
			range_var,
			relation_view.table_name) :
		NULL;
	relation_index = 0U;
	rc = sqlparser_graph_add_relation(
		build,
		block_index,
		cte != NULL ? SQLPARSER_GRAPH_REL_CTE : SQLPARSER_GRAPH_REL_BASE,
		&relation_view,
		&identifiers,
		&relation_index,
		out_error);
	if (rc != 0) {
		return rc;
	}
	selector_index = sqlparser_graph_find_relation_selector_index(build, range_var);
	if (selector_index != (size_t)-1) {
		relation = sqlparser_graph_relation_by_local(build, relation_index);
		if (relation != NULL) {
			relation->selector.kind = SQLPARSER_SELECTOR_KIND_RELATION;
			relation->selector.statement_index = build->statement_index;
			relation->selector.item_index = selector_index;
			relation->has_selector = 1;
			sqlparser_graph_collect_register_relation(
				build,
				relation_index,
				relation);
		}
	}
	if (cte != NULL && cte->ctequery != NULL &&
	    cte->ctequery->node_case == PG_QUERY__NODE__NODE_SELECT_STMT) {
		source_block_index = 0U;
		if (sqlparser_graph_ensure_cte_block(
			    build,
			    cte,
			    &source_block_index,
			    out_error) != 0) {
			return -1;
		}
		relation = sqlparser_graph_relation_by_local(build, relation_index);
		if (relation != NULL) {
			relation->source_block_index = source_block_index;
			relation->has_source_block = 1;
		}
	}
	if (out_relation_index != NULL) {
		*out_relation_index = relation_index;
	}
	return 0;
}

static int sqlparser_graph_add_dml_column_from_res_target(
	sqlparser_graph_build_t *build,
	size_t dml_index,
	size_t ordinal,
	PgQuery__Node *col_node,
	const sqlparser_selector_t *selector,
	size_t *out_column_index,
	sqlparser_error_t *out_error)
{
	sqlparser_graph_dml_column_t column;
	size_t column_index;

	if (out_column_index != NULL) {
		*out_column_index = (size_t)-1;
	}
	if (col_node == NULL ||
	    col_node->node_case != PG_QUERY__NODE__NODE_RES_TARGET ||
	    col_node->res_target == NULL) {
		return 0;
	}
	memset(&column, 0, sizeof(column));
	column.dml_index = dml_index;
	column.ordinal = ordinal;
	column.column_name = col_node->res_target->name;
	if (selector != NULL) {
		column.selector = *selector;
		column.has_selector = 1;
	}
	if (sqlparser_graph_add_dml_column(
		    build, &column, &column_index, out_error) != 0) {
		return -1;
	}
	if (out_column_index != NULL) {
		*out_column_index = column_index;
	}
	return 0;
}

static int sqlparser_graph_dml_expression_sql(
	sqlparser_graph_build_t *build,
	size_t dml_index,
	size_t values_ordinal,
	size_t row_index,
	size_t column_ordinal,
	PgQuery__Node *value_node,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	char *core_sql;
	int source_status;
	sqlparser_status_t status;

	*out_sql = NULL;
	source_status = sqlparser_view_expression_source_sql(
		build->handle,
		&build->origins,
		&build->expression_source_cache,
		value_node,
		out_sql,
		out_error);
	if (source_status != 0) {
		return source_status > 0 ? 0 : -1;
	}
	if (build->handle->generation == 0UL ||
	    build->handle->surface_source_complete) {
		if (build->handle->generation == 0UL &&
		    build->dml_source_sql != NULL) {
			source_status = sqlparser_view_dml_cell_source_sql(
				build->handle,
				build->dml_source_sql,
				0U,
				strlen(build->dml_source_sql),
				values_ordinal,
				row_index,
				column_ordinal,
				out_sql,
				out_error);
		} else if (dml_index == 0U) {
			source_status =
				sqlparser_view_statement_dml_cell_source_sql(
					build->handle,
					build->statement_index,
					values_ordinal,
					row_index,
					column_ordinal,
					out_sql,
					out_error);
		}
	}
	if (source_status != 0) {
		return source_status > 0 ? 0 : -1;
	}
	core_sql = NULL;
	status = sqlparser_render_insert_cell_node_sql(
		build->handle,
		value_node,
		&core_sql,
		out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_postprocess_handle_sql_fragment(
			build->handle,
			build->statement_index,
			core_sql,
			SQLPARSER_FRAGMENT_CONTEXT_EXPRESSION,
			(ProtobufCMessage *const *)&value_node,
			1U,
			"query graph expression cell",
			out_sql,
			out_error);
	}
	free(core_sql);
	if (status != SQLPARSER_STATUS_OK) {
		free(*out_sql);
		*out_sql = NULL;
		return -1;
	}
	if (*out_sql == NULL || (*out_sql)[0] == '\0') {
		free(*out_sql);
		*out_sql = NULL;
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INTERNAL_ERROR,
			"expression cell SQL is missing");
		return -1;
	}
	return 0;
}

static int sqlparser_graph_add_dml_cell_from_node(
	sqlparser_graph_build_t *build,
	size_t dml_index,
	size_t block_index,
	size_t row_index,
	size_t source_row_index,
	size_t column_ordinal,
	PgQuery__Node *value_node,
	size_t values_ordinal,
	const sqlparser_selector_t *selector_override,
	size_t *out_cell_index,
	sqlparser_error_t *out_error)
{
	sqlparser_graph_dml_cell_t cell;
	char *expression_sql;
	size_t cell_index;
	int source_result;

	if (out_cell_index != NULL) {
		*out_cell_index = (size_t)-1;
	}
	memset(&cell, 0, sizeof(cell));
	expression_sql = NULL;
	cell.dml_index = dml_index;
	cell.row_index = row_index;
	cell.column_ordinal = column_ordinal;
	if (sqlparser_graph_fill_dml_value_fields(
		    build,
		    value_node,
		    &cell.kind,
		    &cell.literal,
		    cell.bind,
		    sizeof(cell.bind),
		    &cell.has_bind,
		    &cell.bind_kind,
		    cell.bind_sql,
		    sizeof(cell.bind_sql),
		    &cell.has_bind_sql,
		    &cell.bind_position,
		    &cell.has_bind_position,
		    &cell.selector,
		    &cell.has_selector,
		    out_error) != 0) {
		return -1;
	}
	source_result = sqlparser_graph_fill_dml_source_field(
		    build,
		    block_index,
		    SQLPARSER_CLAUSE_KIND_UNKNOWN,
		    value_node,
		    &cell.kind,
		    &cell.source_field_index,
		    &cell.has_source_field,
		    &cell.source_target_index,
		    &cell.has_source_target,
		    out_error);
	if (source_result < 0 ||
	    (build->collect_relation_bindings && source_result == 0 &&
	     sqlparser_graph_walk_expr(
		     build,
		     block_index,
		     SQLPARSER_CLAUSE_KIND_UNKNOWN,
		     value_node,
		     0U,
		     0,
		     out_error) != 0) ||
	    (cell.kind == SQLPARSER_GRAPH_VALUE_EXPRESSION &&
	     sqlparser_graph_dml_expression_sql(
		     build,
		     dml_index,
		     values_ordinal,
		     source_row_index,
		     column_ordinal,
		     value_node,
		     &expression_sql,
		     out_error) != 0)) {
		free(expression_sql);
		return -1;
	}
	if (selector_override != NULL) {
		cell.selector = *selector_override;
		cell.has_selector = 1;
	}
	if (sqlparser_graph_add_dml_cell(
		    build,
		    &cell,
		    expression_sql,
		    &cell_index,
		    out_error) != 0) {
		free(expression_sql);
		return -1;
	}
	free(expression_sql);
	if (out_cell_index != NULL) {
		*out_cell_index = cell_index;
	}
	return 0;
}

static const char *sqlparser_graph_res_target_assignment_column(
	const sqlparser_handle_t *handle,
	PgQuery__ResTarget *target,
	sqlparser_identifier_source_t *out_source)
{
	PgQuery__Node *last;
	const char *name;
	size_t component_index;
	size_t index;

	if (out_source != NULL) {
		memset(out_source, 0, sizeof(*out_source));
	}

	if (target == NULL) {
		return NULL;
	}
	if (target->n_indirection > 0U && target->indirection != NULL &&
	    sqlparser_node_string_value(
		    target->indirection[target->n_indirection - 1U],
		    &name) &&
	    name != NULL && name[0] != '\0') {
		last = target->indirection[target->n_indirection - 1U];
		component_index = 1U;
		for (index = 0U; index + 1U < target->n_indirection; index++) {
			const char *part;

			if (sqlparser_node_string_value(
				    target->indirection[index],
				    &part)) {
				component_index++;
			}
		}
		if (out_source != NULL && last != NULL &&
		    last->node_case == PG_QUERY__NODE__NODE_STRING &&
		    last->string != NULL) {
			(void)sqlparser_current_identifier_source(
				handle,
				(const char *const *)&last->string->sval,
				target->location,
				component_index,
				out_source);
		}
		return name;
	}
	if (out_source != NULL) {
		(void)sqlparser_current_identifier_source(
			handle,
			(const char *const *)&target->name,
			target->location,
			0U,
			out_source);
	}
	return target->name;
}

static PgQuery__Node *sqlparser_graph_multi_assignment_canonical_source(
	PgQuery__Node **target_list,
	size_t target_count,
	size_t current_index)
{
	PgQuery__MultiAssignRef *current;
	PgQuery__MultiAssignRef *first;
	PgQuery__Node *node;
	size_t first_index;
	size_t preceding_count;

	if (target_list == NULL || current_index >= target_count) {
		return NULL;
	}
	node = target_list[current_index];
	if (node == NULL ||
	    node->node_case != PG_QUERY__NODE__NODE_RES_TARGET ||
	    node->res_target == NULL) {
		return NULL;
	}
	node = sqlparser_unwrap_grouping_node(node->res_target->val);
	if (node == NULL ||
	    node->node_case != PG_QUERY__NODE__NODE_MULTI_ASSIGN_REF ||
	    node->multi_assign_ref == NULL) {
		return NULL;
	}
	current = node->multi_assign_ref;
	if (current->colno <= 0 || current->ncolumns <= 0 ||
	    current->colno > current->ncolumns) {
		return NULL;
	}
	preceding_count = (size_t)(current->colno - 1);
	if (current_index < preceding_count) {
		return NULL;
	}
	first_index = current_index - preceding_count;
	if ((size_t)current->ncolumns > target_count - first_index) {
		return NULL;
	}
	node = target_list[first_index];
	if (node == NULL ||
	    node->node_case != PG_QUERY__NODE__NODE_RES_TARGET ||
	    node->res_target == NULL) {
		return NULL;
	}
	node = sqlparser_unwrap_grouping_node(node->res_target->val);
	if (node == NULL ||
	    node->node_case != PG_QUERY__NODE__NODE_MULTI_ASSIGN_REF ||
	    node->multi_assign_ref == NULL) {
		return NULL;
	}
	first = node->multi_assign_ref;
	return first->colno == 1 &&
		first->ncolumns == current->ncolumns ?
		first->source :
		NULL;
}

static int sqlparser_graph_add_dml_assignment_from_res_target(
	sqlparser_graph_build_t *build,
	size_t dml_index,
	size_t block_index,
	size_t relation_index,
	int has_relation,
	PgQuery__Node *node,
	PgQuery__Node *canonical_multi_source,
	const sqlparser_selector_t *selector,
	size_t *out_assignment_index,
	sqlparser_error_t *out_error)
{
	sqlparser_graph_field_t field;
	sqlparser_graph_dml_assignment_t assignment;
	sqlparser_graph_dml_assignment_t *previous_assignment;
	sqlparser_graph_dml_assignment_t *saved_rhs_capture_assignment;
	sqlparser_graph_dml_t *dml;
	sqlparser_identifier_source_t column_source;
	PgQuery__MultiAssignRef *multi_assign_ref;
	PgQuery__Node *multi_source;
	PgQuery__Node *semantic_value_node;
	PgQuery__Node *value_node;
	size_t field_index;
	size_t assignment_index;
	size_t member_index;
	size_t previous_assignment_index;
	size_t saved_rhs_capture_block_index;
	size_t source_block_index;
	int multi_column_is_valid;
	int source_is_multi_sublink;
	int source_result;
	int walk_result;

	if (out_assignment_index != NULL) {
		*out_assignment_index = (size_t)-1;
	}
	if (node == NULL ||
	    node->node_case != PG_QUERY__NODE__NODE_RES_TARGET ||
	    node->res_target == NULL) {
		return 0;
	}
	if (has_relation &&
	    sqlparser_graph_collect_assignment_target(
		    build,
		    relation_index,
		    node->res_target,
		    out_error) != 0) {
		return -1;
	}
	value_node = node->res_target->val;
	multi_assign_ref = NULL;
	multi_source = NULL;
	multi_column_is_valid = 0;
	semantic_value_node = sqlparser_unwrap_grouping_node(value_node);
	if (semantic_value_node != NULL &&
	    semantic_value_node->node_case ==
		    PG_QUERY__NODE__NODE_MULTI_ASSIGN_REF &&
	    semantic_value_node->multi_assign_ref != NULL) {
		multi_assign_ref = semantic_value_node->multi_assign_ref;
		multi_source = sqlparser_unwrap_grouping_node(
			!build->collect_relation_bindings &&
				canonical_multi_source != NULL ?
				canonical_multi_source :
				multi_assign_ref->source);
		multi_column_is_valid = multi_assign_ref->colno > 0 &&
			multi_assign_ref->ncolumns > 0 &&
			multi_assign_ref->colno <= multi_assign_ref->ncolumns;
		if (multi_column_is_valid &&
		    !build->collect_relation_bindings) {
			member_index =
				(size_t)(multi_assign_ref->colno - 1);
			if (multi_source != NULL &&
			    multi_source->node_case ==
				    PG_QUERY__NODE__NODE_ROW_EXPR &&
			    multi_source->row_expr != NULL &&
			    multi_source->row_expr->args != NULL &&
			    member_index < multi_source->row_expr->n_args &&
			    multi_source->row_expr->args[member_index] != NULL) {
				value_node =
					multi_source->row_expr->args[member_index];
			} else if (multi_source != NULL &&
				   multi_source->node_case ==
					   PG_QUERY__NODE__NODE_LIST &&
				   multi_source->list != NULL &&
				   multi_source->list->items != NULL &&
				   member_index < multi_source->list->n_items &&
				   multi_source->list->items[member_index] != NULL) {
				value_node =
					multi_source->list->items[member_index];
			}
		}
	}
	source_is_multi_sublink = multi_assign_ref != NULL &&
		multi_source != NULL &&
		multi_source->node_case == PG_QUERY__NODE__NODE_SUB_LINK &&
		multi_source->sub_link != NULL;
	memset(&field, 0, sizeof(field));
	field.block_index = block_index;
	field.clause = SQLPARSER_CLAUSE_KIND_SET_LIST;
	field.relation_index = relation_index;
	field.has_relation = has_relation;
	field.column_name = sqlparser_graph_res_target_assignment_column(
		build->handle,
		node->res_target,
		&column_source);
	field.quoted_identifier =
		column_source.known && column_source.delimited;
	if (sqlparser_graph_add_field(build, &field, &field_index, out_error) != 0 ||
	    sqlparser_graph_walk_node_array(
		    build,
		    block_index,
		    SQLPARSER_CLAUSE_KIND_SET_LIST,
		    node->res_target->indirection,
		    node->res_target->n_indirection,
		    out_error) != 0) {
		return -1;
	}
	memset(&assignment, 0, sizeof(assignment));
	assignment.dml_index = dml_index;
	assignment.target_field_index = field_index;
	if (sqlparser_graph_fill_dml_value_fields(
		    build,
		    value_node,
		    &assignment.value_kind,
		    &assignment.literal,
		    assignment.bind,
		    sizeof(assignment.bind),
		    &assignment.has_bind,
		    &assignment.bind_kind,
		    assignment.bind_sql,
		    sizeof(assignment.bind_sql),
		    &assignment.has_bind_sql,
		    &assignment.bind_position,
		    &assignment.has_bind_position,
		    &assignment.selector,
		    &assignment.has_selector,
		    out_error) != 0) {
		return -1;
	}
	source_result = sqlparser_graph_fill_dml_source_field(
		    build,
		    block_index,
		    SQLPARSER_CLAUSE_KIND_SET_LIST,
		    value_node,
		    &assignment.value_kind,
		    &assignment.source_field_index,
		    &assignment.has_source_field,
		    &assignment.source_target_index,
		    &assignment.has_source_target,
		    out_error);
	if (source_result < 0) {
		return -1;
	}
	if (source_result == 0 &&
	    assignment.value_kind == SQLPARSER_GRAPH_VALUE_EXPRESSION) {
		if (source_is_multi_sublink && multi_column_is_valid &&
		    multi_assign_ref->colno > 1) {
			dml = &build->cache->dml[
				build->statement->dml_offset + dml_index];
			if (!build->collect_relation_bindings &&
			    dml->assignments.count > 0U &&
			    dml->assignments.offset <=
				    build->cache->index_pool_count &&
			    dml->assignments.count <=
				    build->cache->index_pool_count -
					    dml->assignments.offset) {
				previous_assignment_index =
					build->cache->index_pool[
						dml->assignments.offset +
						dml->assignments.count - 1U];
				if (previous_assignment_index >=
				    build->cache->dml_assignment_count -
					    build->statement->dml_assignment_offset) {
					previous_assignment = NULL;
				} else {
					previous_assignment =
						&build->cache->dml_assignments[
							build->statement->dml_assignment_offset +
							previous_assignment_index];
				}
				if (previous_assignment != NULL &&
				    previous_assignment->index ==
					    previous_assignment_index &&
				    previous_assignment->statement_index ==
					    build->statement_index &&
				    previous_assignment->dml_index == dml_index &&
				    previous_assignment->value_kind ==
					    SQLPARSER_GRAPH_VALUE_EXPRESSION) {
					assignment.rhs_blocks =
						previous_assignment->rhs_blocks;
				}
			}
		} else {
			saved_rhs_capture_assignment =
				build->rhs_capture_assignment;
			saved_rhs_capture_block_index =
				build->rhs_capture_block_index;
			if (!build->collect_relation_bindings) {
				build->rhs_capture_assignment = &assignment;
				build->rhs_capture_block_index = block_index;
			}
			walk_result = sqlparser_graph_walk_expr(
				build,
				block_index,
				SQLPARSER_CLAUSE_KIND_SET_LIST,
				value_node,
				0U,
				0,
				out_error);
			build->rhs_capture_assignment =
				saved_rhs_capture_assignment;
			build->rhs_capture_block_index =
				saved_rhs_capture_block_index;
			if (walk_result != 0) {
				return -1;
			}
		}
	}
	if (!build->collect_relation_bindings &&
	    source_is_multi_sublink && multi_column_is_valid &&
	    assignment.rhs_blocks.count > 0U &&
	    assignment.rhs_blocks.offset <= build->cache->index_pool_count &&
	    assignment.rhs_blocks.count <=
		    build->cache->index_pool_count -
			    assignment.rhs_blocks.offset) {
		source_block_index = build->cache->index_pool[
			assignment.rhs_blocks.offset +
			assignment.rhs_blocks.count - 1U];
		if (sqlparser_graph_resolve_source_target_from_block(
			    build,
			    source_block_index,
			    (size_t)(multi_assign_ref->colno - 1),
			    &assignment.source_target_index)) {
			assignment.has_source_target = 1;
		}
	}
	if (selector != NULL) {
		assignment.selector = *selector;
		assignment.has_selector = 1;
	}
	if (sqlparser_graph_add_dml_assignment(
		    build,
		    &assignment,
		    &assignment_index,
		    out_error) != 0 ||
	    sqlparser_graph_span_append_index(
		    build,
		    &build->cache->dml[
			    build->statement->dml_offset + dml_index].assignments,
		    assignment_index,
		    out_error) != 0) {
		return -1;
	}
	if (out_assignment_index != NULL) {
		*out_assignment_index = assignment_index;
	}
	return 0;
}

static int sqlparser_graph_add_multi_insert_relation(
	sqlparser_graph_build_t *build,
	const sqlparser_dialect_multi_insert_relation_t *source,
	size_t block_index,
	size_t *out_relation_index,
	sqlparser_error_t *out_error)
{
	sqlparser_graph_relation_identifier_t identifiers;
	sqlparser_relation_view_t relation;

	if (source == NULL) {
		if (out_relation_index != NULL) {
			*out_relation_index = 0U;
		}
		return 0;
	}
	memset(&relation, 0, sizeof(relation));
	relation.database_name = source->database_name;
	relation.schema_name = source->schema_name;
	relation.table_name = source->table_name;
	relation.link_name = NULL;
	memset(&identifiers, 0, sizeof(identifiers));
	if (relation.table_name != NULL) {
		identifiers.object = sqlparser_graph_relation_object_source(
			source->sql);
	}
	if (relation.schema_name != NULL) {
		identifiers.schema = sqlparser_identifier_source_for_text(
			build->handle,
			build->statement_index,
			relation.schema_name);
	}
	if (relation.database_name != NULL) {
		identifiers.database = sqlparser_identifier_source_for_text(
			build->handle,
			build->statement_index,
			relation.database_name);
	}
	if (sqlparser_graph_add_relation(
		    build,
		    block_index,
		    SQLPARSER_GRAPH_REL_BASE,
		    &relation,
		    &identifiers,
		    out_relation_index,
		    out_error) != 0) {
		return -1;
	}
	return 0;
}

static int sqlparser_graph_add_multi_insert_dml_column(
	sqlparser_graph_build_t *build,
	size_t dml_index,
	size_t ordinal,
	const sqlparser_dialect_multi_insert_column_t *source,
	sqlparser_graph_dml_branch_t *branch,
	sqlparser_error_t *out_error)
{
	sqlparser_graph_dml_column_t column;
	size_t column_index;

	memset(&column, 0, sizeof(column));
	column.dml_index = dml_index;
	column.ordinal = ordinal;
	column.column_name = source != NULL ? source->name : NULL;
	if (sqlparser_graph_add_dml_column(build, &column, &column_index, out_error) != 0) {
		return -1;
	}
	if (branch != NULL &&
	    sqlparser_graph_span_append_index(build, &branch->target_columns, column_index, out_error) != 0) {
		return -1;
	}
	return 0;
}

static int sqlparser_graph_multi_insert_cell_resolve_source_target(
	sqlparser_graph_build_t *build,
	size_t source_block_index,
	PgQuery__ColumnRef *column_ref,
	const char *source_sql,
	size_t *out_source_target_index)
{
	sqlparser_identifier_source_t name_identifier;
	const char *name;
	const char *qualifier;
	size_t index;
	size_t match_index;
	size_t match_count;

	if (out_source_target_index != NULL) {
		*out_source_target_index = 0U;
	}
	if (build == NULL || column_ref == NULL) {
		return 0;
	}
	if (sqlparser_graph_column_ref_is_pseudo(build, column_ref) ||
	    sqlparser_graph_fragment_column_ref_is_dialect_expression(
		    build,
		    column_ref,
		    source_sql)) {
		return 0;
	}
	name = sqlparser_graph_column_ref_part(column_ref, 0U);
	qualifier = sqlparser_graph_column_ref_part(column_ref, 1U);
	if (name == NULL || name[0] == '\0' || strcmp(name, "*") == 0 ||
	    (qualifier != NULL && qualifier[0] != '\0')) {
		return 0;
	}
	name_identifier =
		sqlparser_graph_fragment_identifier_source(source_sql);
	match_index = 0U;
	match_count = 0U;
	for (index = 0U; index < sqlparser_graph_local_target_count(build); index++) {
		sqlparser_identifier_source_t *target_identifier;
		sqlparser_graph_target_cache_t *target;
		int matched;

		target = sqlparser_graph_target_by_local(build, index);
		target_identifier =
			sqlparser_graph_target_identifier_by_local(build, index);
		if (target == NULL ||
		    target->block_index != source_block_index ||
		    target->kind != SQLPARSER_GRAPH_TARGET_FIELD) {
			continue;
		}
		matched = target->output_name != NULL &&
			target_identifier != NULL &&
			sqlparser_identifier_semantic_equal(
				build->handle,
				target->output_name,
				*target_identifier,
				name,
				name_identifier);
		if (!matched) {
			continue;
		}
		match_index = index;
		match_count++;
		if (match_count > 1U) {
			return 0;
		}
	}
	if (match_count == 1U && out_source_target_index != NULL) {
		*out_source_target_index = match_index;
	}
	return match_count == 1U;
}

static int sqlparser_graph_add_multi_insert_dml_cell(
	sqlparser_graph_build_t *build,
	size_t dml_index,
	size_t branch_ordinal,
	size_t column_ordinal,
	size_t source_block_index,
	int has_source_block,
	const sqlparser_dialect_multi_insert_value_t *source,
	sqlparser_graph_dml_branch_t *branch,
	sqlparser_error_t *out_error)
{
	sqlparser_graph_dml_cell_t cell;
	const char *expression_sql;
	PgQuery__Node *node;
	PgQuery__Node *semantic_node;
	size_t cell_index;

	if (source == NULL) {
		return 0;
	}
	memset(&cell, 0, sizeof(cell));
	expression_sql = NULL;
	cell.dml_index = dml_index;
	cell.row_index = branch_ordinal;
	cell.column_ordinal = column_ordinal;
	if (source->has_bind) {
		cell.kind = SQLPARSER_GRAPH_VALUE_BIND;
		cell.has_bind = 1;
		cell.bind_kind = source->bind_kind;
		sqlparser_view_copy_public_text(cell.bind, sizeof(cell.bind), source->bind, NULL);
		sqlparser_view_copy_public_text(cell.bind_sql, sizeof(cell.bind_sql), source->bind_sql, NULL);
		cell.has_bind_sql = cell.bind_sql[0] != '\0';
		cell.bind_position = source->bind_position;
		cell.has_bind_position = source->has_bind_position;
	} else if (source->has_literal) {
		cell.kind = SQLPARSER_GRAPH_VALUE_LITERAL;
		cell.literal = source->literal;
	} else if (source->parser_sql != NULL &&
		   !sqlparser_view_parser_sql_has_bind(source->parser_sql)) {
		node = NULL;
		if (sqlparser_parse_insert_cell_node_sql(
			    source->parser_sql,
			    NULL,
			    &node,
			    out_error) != SQLPARSER_STATUS_OK) {
			return -1;
		}
		semantic_node = sqlparser_unwrap_grouping_node(node);
		if (semantic_node != NULL &&
		    semantic_node->node_case == PG_QUERY__NODE__NODE_A_CONST &&
		    semantic_node->a_const != NULL) {
			cell.kind = SQLPARSER_GRAPH_VALUE_LITERAL;
			if (sqlparser_fill_literal_view_from_a_const(semantic_node->a_const, &cell.literal, out_error) != SQLPARSER_STATUS_OK) {
				sqlparser_free_proto_node(node);
				return -1;
			}
		} else if (semantic_node != NULL &&
		           semantic_node->node_case == PG_QUERY__NODE__NODE_SET_TO_DEFAULT) {
			cell.kind = SQLPARSER_GRAPH_VALUE_DEFAULT;
		} else if (has_source_block &&
		           semantic_node != NULL &&
		           semantic_node->node_case == PG_QUERY__NODE__NODE_COLUMN_REF &&
		           semantic_node->column_ref != NULL &&
		           sqlparser_graph_multi_insert_cell_resolve_source_target(
			           build,
			           source_block_index,
			           semantic_node->column_ref,
			           source->public_sql,
			           &cell.source_target_index)) {
			sqlparser_graph_target_cache_t *target;

			cell.kind = SQLPARSER_GRAPH_VALUE_FIELD;
			cell.has_source_target = 1;
			target = sqlparser_graph_target_by_local(build, cell.source_target_index);
			if (target != NULL &&
			    (target->flags &
			     SQLPARSER_GRAPH_TARGET_HAS_FIELD) != 0U) {
				cell.source_field_index = target->field_index;
				cell.has_source_field = 1;
			}
		} else {
			cell.kind = SQLPARSER_GRAPH_VALUE_EXPRESSION;
		}
		sqlparser_free_proto_node(node);
	} else {
		cell.kind = SQLPARSER_GRAPH_VALUE_EXPRESSION;
	}
	cell.selector.kind = SQLPARSER_SELECTOR_KIND_INSERT_CELL;
	cell.selector.statement_index = build->statement_index;
	cell.selector.row_index = branch_ordinal;
	cell.selector.column_index = column_ordinal;
	cell.has_selector = 1;
	if (cell.kind == SQLPARSER_GRAPH_VALUE_EXPRESSION) {
		if (source->public_sql == NULL || source->public_sql[0] == '\0') {
			sqlparser_error_set_message(
				out_error,
				SQLPARSER_STATUS_INTERNAL_ERROR,
				"multi-insert expression cell SQL is missing");
			return -1;
		}
		expression_sql = source->public_sql;
	}
	if (sqlparser_graph_add_dml_cell(
		    build,
		    &cell,
		    expression_sql,
		    &cell_index,
		    out_error) != 0) {
		return -1;
	}
	if (branch != NULL &&
	    sqlparser_graph_span_append_index(build, &branch->rows, cell_index, out_error) != 0) {
		return -1;
	}
	return 0;
}

static int sqlparser_graph_parse_identifier_parts(
	const char *sql,
	char **out_storage,
	char **parts,
	int *quoted_parts,
	size_t part_capacity,
	size_t *out_count,
	sqlparser_error_t *out_error)
{
	char *storage;
	size_t len;
	size_t read_pos;
	size_t write_pos;
	size_t count;

	*out_storage = NULL;
	*out_count = 0U;
	if (sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "DML result sink identifier is missing");
		return -1;
	}
	len = strlen(sql);
	storage = (char *)malloc(len + 1U);
	if (storage == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return -1;
	}
	read_pos = 0U;
	write_pos = 0U;
	count = 0U;
	while (read_pos < len) {
		char close_char;
		int quoted;
		int closed;

		while (read_pos < len && isspace((unsigned char)sql[read_pos])) {
			read_pos++;
		}
		if (read_pos >= len || count >= part_capacity) {
			goto invalid;
		}
		parts[count] = storage + write_pos;
		quoted = sql[read_pos] == '[' || sql[read_pos] == '"';
		if (quoted_parts != NULL) {
			quoted_parts[count] = quoted;
		}
		count++;
		close_char = sql[read_pos] == '[' ? ']' : '"';
		closed = !quoted;
		if (quoted) {
			read_pos++;
		}
		while (read_pos < len) {
			char ch;

			ch = sql[read_pos];
			if (quoted) {
				if (ch == close_char) {
					if (read_pos + 1U < len && sql[read_pos + 1U] == close_char) {
						storage[write_pos++] = close_char;
						read_pos += 2U;
						continue;
					}
					read_pos++;
					closed = 1;
					break;
				}
			} else if (ch == '.' || isspace((unsigned char)ch)) {
				break;
			}
			storage[write_pos++] = ch;
			read_pos++;
		}
		if (!closed || parts[count - 1U] == storage + write_pos) {
			goto invalid;
		}
		storage[write_pos++] = '\0';
		while (read_pos < len && isspace((unsigned char)sql[read_pos])) {
			read_pos++;
		}
		if (read_pos == len) {
			break;
		}
		if (sql[read_pos] != '.') {
			goto invalid;
		}
		read_pos++;
	}
	if (count == 0U) {
		goto invalid;
	}
	*out_storage = storage;
	*out_count = count;
	return 0;

invalid:
	free(storage);
	sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "DML result sink identifier is invalid");
	return -1;
}

static int sqlparser_graph_add_dml_result_sink_relation(
	sqlparser_graph_build_t *build,
	size_t block_index,
	size_t dml_index,
	size_t channel_index,
	const char *sink_sql,
	sqlparser_graph_dml_result_t *result,
	sqlparser_error_t *out_error)
{
	sqlparser_relation_view_t relation_view;
	sqlparser_graph_relation_t *relation;
	char *storage;
	char *parts[4];
	int quoted_parts[4];
	size_t part_count;
	size_t relation_index;

	storage = NULL;
	memset(parts, 0, sizeof(parts));
	memset(quoted_parts, 0, sizeof(quoted_parts));
	if (sqlparser_graph_parse_identifier_parts(
		    sink_sql,
		    &storage,
		    parts,
		    quoted_parts,
		    sizeof(parts) / sizeof(parts[0]),
		    &part_count,
		    out_error) != 0) {
		return -1;
	}
	if (part_count > 3U) {
		free(storage);
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_UNSUPPORTED, "SQL Server OUTPUT INTO does not support a remote target");
		return -1;
	}
	memset(&relation_view, 0, sizeof(relation_view));
	relation_view.table_name = parts[part_count - 1U];
	if (part_count > 1U) {
		relation_view.schema_name = parts[part_count - 2U];
	}
	if (part_count > 2U) {
		relation_view.database_name = parts[part_count - 3U];
	}
	if (sqlparser_graph_dml_result_own_text(build, storage, out_error) != 0) {
		return -1;
	}
	if (sqlparser_graph_add_relation(
		    build,
		    block_index,
		    SQLPARSER_GRAPH_REL_BASE,
		    &relation_view,
		    NULL,
		    &relation_index,
		    out_error) != 0) {
		return -1;
	}
	relation = sqlparser_graph_relation_by_local(build, relation_index);
	if (relation == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "DML result sink relation is missing");
		return -1;
	}
	relation->quoted_identifier = quoted_parts[part_count - 1U];
	relation->selector.kind = SQLPARSER_SELECTOR_KIND_DML_RESULT_SINK;
	relation->selector.statement_index = build->statement_index;
	relation->selector.item_index = dml_index;
	relation->selector.row_index = channel_index;
	relation->has_selector = 1;
	result->sink_relation_index = relation_index;
	result->has_sink_relation = 1;
	return 0;
}

static int sqlparser_graph_add_dml_result_sink_column(
	sqlparser_graph_build_t *build,
	size_t dml_index,
	size_t channel_index,
	size_t ordinal,
	const char *column_sql,
	sqlparser_graph_dml_result_t *result,
	sqlparser_error_t *out_error)
{
	sqlparser_graph_dml_column_t column;
	char *storage;
	char *parts[2];
	size_t part_count;
	size_t column_index;

	storage = NULL;
	memset(parts, 0, sizeof(parts));
	if (sqlparser_graph_parse_identifier_parts(
		    column_sql,
		    &storage,
		    parts,
		    NULL,
		    sizeof(parts) / sizeof(parts[0]),
		    &part_count,
		    out_error) != 0) {
		return -1;
	}
	if (part_count != 1U) {
		free(storage);
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "SQL Server OUTPUT INTO column must be a single identifier");
		return -1;
	}
	if (sqlparser_graph_dml_result_own_text(build, storage, out_error) != 0) {
		return -1;
	}
	memset(&column, 0, sizeof(column));
	column.dml_index = dml_index;
	column.ordinal = ordinal;
	column.column_name = parts[0];
	column.selector.kind = SQLPARSER_SELECTOR_KIND_DML_RESULT_SINK_COLUMN;
	column.selector.statement_index = build->statement_index;
	column.selector.item_index = dml_index;
	column.selector.row_index = channel_index;
	column.selector.column_index = ordinal;
	column.has_selector = 1;
	if (sqlparser_graph_add_dml_column(build, &column, &column_index, out_error) != 0) {
		return -1;
	}
	return sqlparser_graph_span_append_index(build, &result->sink_columns, column_index, out_error);
}

static int sqlparser_graph_build_dml_results(
	sqlparser_graph_build_t *build,
	size_t dml_index,
	size_t dml_block_index,
	ProtobufCMessage *message,
	PgQuery__Node **returning_list,
	size_t returning_count,
	sqlparser_error_t *out_error)
{
	sqlparser_dialect_dml_result_dml_t dml_info;
	const sqlparser_dialect_dml_result_dml_t *inventory_dml;
	sqlparser_graph_dml_t *dml;
	size_t channel_index;
	size_t expected_count;
	size_t meta_index;

	memset(&dml_info, 0, sizeof(dml_info));
	if (build == NULL || build->handle == NULL) {
		return 0;
	}
	inventory_dml = sqlparser_graph_dml_inventory_at(build, dml_index);
	if (inventory_dml != NULL) {
		dml_info = *inventory_dml;
	} else if (!sqlparser_dialect_dml_result_dml_at(
			   build->handle,
			   build->statement_index,
			   dml_index,
			   &dml_info)) {
		return 0;
	}
	dml = &build->cache->dml[build->statement->dml_offset + dml_index];
	if (dml_info.kind != dml->kind ||
	    (dml_info.message != NULL && dml_info.message != message)) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "DML result metadata does not match the statement AST");
		return -1;
	}
	if (sqlparser_graph_dml_result_add_meta(
		build,
		dml_index,
		dml_info.parent_dml_index,
		dml_info.has_parent,
		&meta_index,
		out_error) != 0) {
		return -1;
	}
	expected_count = 0U;
	for (channel_index = 0U; channel_index < dml_info.channel_count; channel_index++) {
		sqlparser_dialect_dml_result_channel_t channel;

		if (!sqlparser_graph_dml_result_channel_at(
			    build,
			    dml_index,
			    channel_index,
			    &channel) ||
		    channel.target_offset != expected_count ||
		    channel.target_count > SIZE_MAX - expected_count) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "DML result channel metadata is invalid");
			return -1;
		}
		expected_count += channel.target_count;
		if ((channel.sink_value_count > 0U &&
		     channel.sink_value_offset != expected_count) ||
		    channel.sink_value_count > SIZE_MAX - expected_count) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "DML result sink value metadata is invalid");
			return -1;
		}
		expected_count += channel.sink_value_count;
	}
	if (expected_count != returning_count ||
	    (returning_count > 0U && returning_list == NULL)) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "DML result target metadata does not match the statement AST");
		return -1;
	}
	for (channel_index = 0U; channel_index < dml_info.channel_count; channel_index++) {
		sqlparser_dialect_dml_result_channel_t channel;
		sqlparser_graph_dml_result_t result_value;
		sqlparser_graph_dml_result_t *result;
		sqlparser_graph_dml_result_build_state_t saved_state;
		size_t result_index;
		size_t result_block_index;
		size_t sink_target_index;
		size_t target_ordinal;

		memset(&channel, 0, sizeof(channel));
		(void)sqlparser_graph_dml_result_channel_at(
			build,
			dml_index,
			channel_index,
			&channel);
		if (sqlparser_graph_add_block(
			    build,
			    SQLPARSER_GRAPH_BLOCK_DML_RESULT,
			    &result_block_index,
			    out_error) != 0) {
			return -1;
		}
		memset(&result_value, 0, sizeof(result_value));
		result_value.kind = channel.kind;
		result_value.block_index = result_block_index;
		if (sqlparser_graph_dml_result_add_result(
			    build,
			    meta_index,
			    &result_value,
			    &result_index,
			    out_error) != 0) {
			return -1;
		}
		result = &build->cache->dml_results->results[result_index];
		if (channel.kind == SQLPARSER_GRAPH_DML_RESULT_SINK &&
		    channel.sink_sql != NULL &&
		    sqlparser_graph_add_dml_result_sink_relation(
			    build,
			    dml_block_index,
			    dml_index,
			    channel_index,
			    channel.sink_sql,
			    result,
			    out_error) != 0) {
			return -1;
		}
		sqlparser_graph_dml_result_build_state_save(build, &saved_state);
		build->building_dml_result = 1;
		build->dml_result_scope_block_index = dml_block_index;
		build->dml_result_target_relation_index = dml->target_relation_index;
		build->dml_result_has_target_relation = dml->has_target_relation;
		build->dml_result_target_reference_kinds =
			dml_info.target_reference_kinds;
		build->dml_result_index = result_index;
		build->has_dml_result = 1;
		if (channel.sink_value_count != 0U &&
		    channel.target_count != channel.sink_value_count) {
			sqlparser_graph_dml_result_build_state_restore(
				build, &saved_state);
			sqlparser_error_set_message(
				out_error,
				SQLPARSER_STATUS_INTERNAL_ERROR,
				"DML result sink value metadata is invalid");
			return -1;
		}
		sink_target_index = 0U;
		for (target_ordinal = 0U; target_ordinal < channel.target_count; target_ordinal++) {
			PgQuery__Node *target_node;

			build->dml_result_action_marker = sqlparser_dialect_dml_result_action_marker_at(
				build->handle->dialect,
				build->handle->dialect_state,
				build->statement_index,
				dml_index,
				channel_index,
				target_ordinal);
			target_node = returning_list[channel.target_offset + target_ordinal];
			if (target_node == NULL ||
			    target_node->node_case != PG_QUERY__NODE__NODE_RES_TARGET ||
			    target_node->res_target == NULL ||
			    sqlparser_graph_build_target(
				    build,
				    result_block_index,
				    SQLPARSER_SELECTOR_KIND_DML_RESULT_TARGETS,
				    SQLPARSER_SELECTOR_KIND_DML_RESULT_TARGET,
				    dml_index,
				    channel_index,
				    target_ordinal,
				    SQLPARSER_CLAUSE_KIND_DML_RESULT,
				    target_node->res_target,
				    channel.sink_value_count != 0U ?
					    &sink_target_index : NULL,
				    out_error) != 0) {
				sqlparser_graph_dml_result_build_state_restore(
					build, &saved_state);
				return -1;
			}
			if (channel.sink_value_count != 0U) {
				PgQuery__Node *sink_node;
				PgQuery__ResTarget *sink_target;
				sqlparser_graph_target_cache_t *target;
				size_t value_index;
				int added;

				sink_node = returning_list[
					channel.sink_value_offset + target_ordinal];
				sink_target = sink_node != NULL &&
					sink_node->node_case ==
						PG_QUERY__NODE__NODE_RES_TARGET ?
						sink_node->res_target : NULL;
				target = sqlparser_graph_target_by_local(
					build, sink_target_index);
				value_index = 0U;
				added = 0;
				if (sink_target == NULL || sink_target->val == NULL ||
				    sink_target->val->node_case !=
					    PG_QUERY__NODE__NODE_PARAM_REF ||
				    target == NULL ||
				    sqlparser_graph_add_target_value_from_node(
					    build,
					    result_block_index,
					    SQLPARSER_CLAUSE_KIND_DML_RESULT,
					    sink_target->val,
					    &value_index,
					    &added,
					    out_error) != 0 ||
				    !added) {
					sqlparser_graph_dml_result_build_state_restore(
						build, &saved_state);
					if (out_error != NULL &&
					    out_error->code == SQLPARSER_STATUS_OK) {
						sqlparser_error_set_message(
							out_error,
							SQLPARSER_STATUS_INTERNAL_ERROR,
							"DML result sink value is invalid");
					}
					return -1;
				}
				target->sink_value_index = value_index;
				target->flags |=
					SQLPARSER_GRAPH_TARGET_HAS_SINK_VALUE;
			}
		}
		sqlparser_graph_dml_result_build_state_restore(build, &saved_state);
		if (result_index >= build->cache->dml_results->result_count) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "DML result index is invalid after target traversal");
			return -1;
		}
		result = &build->cache->dml_results->results[result_index];
		for (target_ordinal = 0U; target_ordinal < channel.sink_column_count; target_ordinal++) {
			const char *column_sql;

			column_sql = sqlparser_dialect_dml_result_sink_column_at(
				build->handle->dialect,
				build->handle->dialect_state,
				build->statement_index,
				dml_index,
				channel_index,
				target_ordinal);
			if (column_sql == NULL ||
			    sqlparser_graph_add_dml_result_sink_column(
				    build,
				    dml_index,
				    channel_index,
				    target_ordinal,
				    column_sql,
				    result,
				    out_error) != 0) {
				return -1;
			}
		}
	}
	return 0;
}

static int sqlparser_graph_build_multi_insert_dml(
	sqlparser_graph_build_t *build,
	PgQuery__InsertStmt *stmt,
	const sqlparser_dialect_multi_insert_t *multi,
	sqlparser_error_t *out_error)
{
	sqlparser_graph_dml_t dml;
	sqlparser_graph_dml_t *dml_item;
	size_t root_block_index;
	size_t dml_index;
	size_t branch_index;
	size_t *local_branch_indices;

	if (build == NULL || stmt == NULL || multi == NULL) {
		return 0;
	}
	local_branch_indices = NULL;
	memset(&dml, 0, sizeof(dml));
	dml.kind = SQLPARSER_GRAPH_DML_INSERT;
	dml.insert_mode = multi->mode == SQLPARSER_DIALECT_MULTI_INSERT_FIRST ?
		SQLPARSER_GRAPH_INSERT_MODE_FIRST :
		SQLPARSER_GRAPH_INSERT_MODE_ALL;
	if (sqlparser_graph_add_block(build, SQLPARSER_GRAPH_BLOCK_SELECT, &root_block_index, out_error) != 0 ||
	    sqlparser_graph_add_dml(build, &dml, (ProtobufCMessage *)stmt, &dml_index, out_error) != 0) {
		return -1;
	}
	if (multi->branch_count > 0U) {
		local_branch_indices = (size_t *)calloc(multi->branch_count, sizeof(*local_branch_indices));
		if (local_branch_indices == NULL) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return -1;
		}
	}
	dml_item = &build->cache->dml[build->statement->dml_offset + dml_index];
	if (stmt->select_stmt != NULL &&
	    stmt->select_stmt->node_case == PG_QUERY__NODE__NODE_SELECT_STMT) {
		dml_item->has_source_block = 1;
		if (sqlparser_graph_build_select(
			    build,
			    stmt->select_stmt->select_stmt,
			    SQLPARSER_GRAPH_BLOCK_SELECT,
			    &dml_item->source_block_index,
			    out_error) != 0) {
			free(local_branch_indices);
			return -1;
		}
	}
	for (branch_index = 0U; branch_index < multi->branch_count; branch_index++) {
		const sqlparser_dialect_multi_insert_branch_t *source_branch;
		sqlparser_graph_dml_branch_t branch;
		size_t local_branch_index;

		source_branch = &multi->branches[branch_index];
		memset(&branch, 0, sizeof(branch));
		branch.dml_index = dml_index;
		branch.ordinal = branch_index;
		if (source_branch->has_condition) {
			branch.branch_kind = SQLPARSER_GRAPH_DML_BRANCH_WHEN;
		} else if (source_branch->is_else) {
			branch.branch_kind = SQLPARSER_GRAPH_DML_BRANCH_ELSE;
		} else {
			branch.branch_kind = SQLPARSER_GRAPH_DML_BRANCH_UNCONDITIONAL;
		}
		if (sqlparser_graph_add_multi_insert_relation(
			    build,
			    &source_branch->relation,
			    root_block_index,
			    &branch.target_relation_index,
			    out_error) != 0) {
			free(local_branch_indices);
			return -1;
		}
		branch.has_target_relation = source_branch->relation.table_name != NULL;
		if (source_branch->has_condition) {
			branch.condition_selector.kind = SQLPARSER_SELECTOR_KIND_INSERT_BRANCH_CONDITION;
			branch.condition_selector.statement_index = build->statement_index;
			branch.condition_selector.item_index = branch_index;
			branch.has_condition_selector = 1;
		}
		if (sqlparser_graph_add_dml_branch(
			    build,
			    &branch,
			    NULL,
			    &local_branch_index,
			    out_error) != 0 ||
		    sqlparser_graph_span_append_index(build, &dml_item->branches, local_branch_index, out_error) != 0) {
			free(local_branch_indices);
			return -1;
		}
		local_branch_indices[branch_index] = local_branch_index;
	}
	for (branch_index = 0U; branch_index < multi->branch_count; branch_index++) {
		const sqlparser_dialect_multi_insert_branch_t *source_branch;
		sqlparser_graph_dml_branch_t *branch_item;
		size_t index;

		source_branch = &multi->branches[branch_index];
		branch_item = &build->cache->dml_branches[build->statement->dml_branch_offset + local_branch_indices[branch_index]];
		for (index = 0U; index < source_branch->column_count; index++) {
			if (sqlparser_graph_add_multi_insert_dml_column(
				    build,
				    dml_index,
				    index,
				    &source_branch->columns[index],
				    branch_item,
				    out_error) != 0) {
				free(local_branch_indices);
				return -1;
			}
		}
		for (index = 0U; index < source_branch->cell_count; index++) {
			if (sqlparser_graph_add_multi_insert_dml_cell(
				    build,
				    dml_index,
				    branch_index,
				    index,
				    dml_item->source_block_index,
				    dml_item->has_source_block,
				    &source_branch->cells[index],
				    branch_item,
				    out_error) != 0) {
				free(local_branch_indices);
				return -1;
			}
		}
	}
	free(local_branch_indices);
	return 0;
}

static int sqlparser_graph_build_insert_dml(
	sqlparser_graph_build_t *build,
	PgQuery__InsertStmt *stmt,
	sqlparser_error_t *out_error)
{
	sqlparser_graph_dml_t dml;
	sqlparser_graph_dml_t *dml_item;
	sqlparser_graph_scope_t scope;
	size_t block_index;
	size_t dml_index;
	size_t index;
	PgQuery__SelectStmt *values_stmt;
	int result_status;

	if (build == NULL || stmt == NULL) {
		return 0;
	}
	memset(&dml, 0, sizeof(dml));
	dml.kind = SQLPARSER_GRAPH_DML_INSERT;
	dml.insert_mode = SQLPARSER_GRAPH_INSERT_MODE_VALUES;
	if (sqlparser_graph_add_block(build, SQLPARSER_GRAPH_BLOCK_SELECT, &block_index, out_error) != 0 ||
	    sqlparser_graph_add_dml_target_relation(build, stmt->relation, block_index, &dml.target_relation_index, out_error) != 0) {
		return -1;
	}
	dml.has_target_relation = stmt->relation != NULL;
	if (sqlparser_graph_add_dml(build, &dml, (ProtobufCMessage *)stmt, &dml_index, out_error) != 0) {
		return -1;
	}
	dml_item = &build->cache->dml[
		build->statement->dml_offset + dml_index];
	for (index = 0U; index < stmt->n_cols; index++) {
		size_t column_index;

		if (sqlparser_graph_add_dml_column_from_res_target(
			    build,
			    dml_index,
			    index,
			    stmt->cols != NULL ? stmt->cols[index] : NULL,
			    NULL,
			    &column_index,
			    out_error) != 0 ||
		    (column_index != (size_t)-1 &&
		     sqlparser_graph_span_append_index(
			     build,
			     &build->cache->dml[
				     build->statement->dml_offset + dml_index]
				      .target_columns,
			     column_index,
			     out_error) != 0)) {
			return -1;
		}
	}
	sqlparser_graph_push_scope(
		build,
		&scope,
		block_index,
		stmt->with_clause);
	values_stmt = NULL;
	if (sqlparser_insert_source_from_stmt(stmt) == SQLPARSER_INSERT_SOURCE_VALUES &&
	    stmt->select_stmt != NULL &&
	    stmt->select_stmt->node_case == PG_QUERY__NODE__NODE_SELECT_STMT &&
	    (values_stmt = stmt->select_stmt->select_stmt) != NULL &&
	    values_stmt != NULL &&
	    values_stmt->values_lists != NULL) {
		for (index = 0U; index < values_stmt->n_values_lists; index++) {
			PgQuery__Node *row_node;
			size_t column_index;

			row_node = values_stmt->values_lists[index];
			if (row_node == NULL ||
			    row_node->node_case != PG_QUERY__NODE__NODE_LIST ||
			    row_node->list == NULL) {
				continue;
			}
			for (column_index = 0U; column_index < row_node->list->n_items; column_index++) {
				sqlparser_selector_t selector;
				size_t cell_index;
				int assign_insert_selector;

				memset(&selector, 0, sizeof(selector));
				selector.kind = SQLPARSER_SELECTOR_KIND_INSERT_CELL;
				selector.statement_index = build->statement_index;
				selector.row_index = index;
				selector.column_index = column_index;
				assign_insert_selector =
					build->statement_node != NULL &&
					build->statement_node->node_case ==
						PG_QUERY__NODE__NODE_INSERT_STMT &&
					build->statement_node->insert_stmt == stmt;

				if (sqlparser_graph_add_dml_cell_from_node(
					    build,
					    dml_index,
					    block_index,
					    index,
					    index,
					    column_index,
					    row_node->list->items[column_index],
					    0U,
					    assign_insert_selector ? &selector : NULL,
					    &cell_index,
					    out_error) != 0 ||
				    (cell_index != (size_t)-1 &&
				     sqlparser_graph_span_append_index(
					     build,
					     &build->cache->dml[
						     build->statement->dml_offset +
						     dml_index]
						      .rows,
					     cell_index,
					     out_error) != 0)) {
					sqlparser_graph_pop_scope(build);
					return -1;
				}
			}
		}
	} else if (stmt->select_stmt != NULL &&
			   stmt->select_stmt->node_case == PG_QUERY__NODE__NODE_SELECT_STMT) {
		dml_item->insert_mode = SQLPARSER_GRAPH_INSERT_MODE_SELECT;
		dml_item->has_source_block = 1;
		if (sqlparser_graph_build_select(
			    build,
			    stmt->select_stmt->select_stmt,
			    SQLPARSER_GRAPH_BLOCK_SELECT,
			    &dml_item->source_block_index,
			    out_error) != 0) {
			sqlparser_graph_pop_scope(build);
			return -1;
		}
	}
	if (sqlparser_graph_ensure_ctes(
		    build,
		    stmt->with_clause,
		    out_error) != 0) {
		sqlparser_graph_pop_scope(build);
		return -1;
	}
	if (stmt->on_conflict_clause != NULL) {
		PgQuery__InferClause *infer;
		int conflict_status;
		int saved_in_on_conflict_update;
		size_t saved_on_conflict_target_block_index;

		infer = stmt->on_conflict_clause->infer;
		conflict_status = 0;
		saved_in_on_conflict_update = build->in_on_conflict_update;
		saved_on_conflict_target_block_index =
			build->on_conflict_target_block_index;
		build->in_on_conflict_update = 0;
		build->on_conflict_target_block_index = block_index;
		if (build->collect_relation_bindings && infer != NULL) {
			for (index = 0U; index < infer->n_index_elems; index++) {
				PgQuery__Node *node;

				node = infer->index_elems != NULL ?
					infer->index_elems[index] : NULL;
				if (node != NULL &&
				    node->node_case == PG_QUERY__NODE__NODE_INDEX_ELEM &&
				    node->index_elem != NULL &&
				    sqlparser_graph_walk_expr(
					    build,
					    block_index,
					    SQLPARSER_CLAUSE_KIND_UNKNOWN,
					    node->index_elem->expr,
					    0U,
					    0,
					    out_error) != 0) {
					conflict_status = -1;
					break;
				}
			}
			if (conflict_status == 0 &&
			    sqlparser_graph_walk_predicate_expr(
				    build,
				    block_index,
				    SQLPARSER_CLAUSE_KIND_WHERE,
				    infer->where_clause,
				    out_error) != 0) {
				conflict_status = -1;
			}
		}
		if (conflict_status == 0 &&
		    stmt->on_conflict_clause->action ==
			    PG_QUERY__ON_CONFLICT_ACTION__ONCONFLICT_UPDATE) {
			build->in_on_conflict_update = 1;
			for (index = 0U;
			     index < stmt->on_conflict_clause->n_target_list;
			     index++) {
				sqlparser_selector_t selector;

				memset(&selector, 0, sizeof(selector));
				selector.kind = SQLPARSER_SELECTOR_KIND_ASSIGNMENT;
				selector.statement_index = build->statement_index;
				selector.item_index = index;
				if (build->statement_node == NULL ||
				    build->statement_node->node_case !=
					    PG_QUERY__NODE__NODE_INSERT_STMT ||
				    build->statement_node->insert_stmt != stmt) {
					selector.row_index = dml_index + 1U;
				}
				if (sqlparser_graph_add_dml_assignment_from_res_target(
					    build,
					    dml_index,
					    block_index,
					    dml.target_relation_index,
					    dml.has_target_relation,
					    stmt->on_conflict_clause->target_list != NULL ?
					    stmt->on_conflict_clause->target_list[index] :
						    NULL,
					    sqlparser_graph_multi_assignment_canonical_source(
						    stmt->on_conflict_clause->target_list,
						    stmt->on_conflict_clause->n_target_list,
						    index),
					    &selector,
					    NULL,
					    out_error) != 0) {
					conflict_status = -1;
					break;
				}
			}
			if (conflict_status == 0 &&
			    build->collect_relation_bindings &&
			    sqlparser_graph_walk_predicate_expr(
				    build,
				    block_index,
				    SQLPARSER_CLAUSE_KIND_WHERE,
				    stmt->on_conflict_clause->where_clause,
				    out_error) != 0) {
				conflict_status = -1;
			}
		}
		build->in_on_conflict_update = saved_in_on_conflict_update;
		build->on_conflict_target_block_index =
			saved_on_conflict_target_block_index;
		if (conflict_status != 0) {
			sqlparser_graph_pop_scope(build);
			return -1;
		}
	}
	if (build->handle != NULL &&
	    build->handle->dialect_ops != NULL &&
	    build->handle->dialect_ops->insert_mode != NULL) {
		sqlparser_graph_dml_t *dml_item;

		dml_item = &build->cache->dml[build->statement->dml_offset + dml_index];
		dml_item->insert_mode = build->handle->dialect_ops->insert_mode(
			build->handle->dialect_state,
			build->statement_index,
			dml_item->insert_mode);
	}
	result_status = sqlparser_graph_build_dml_results(
		build,
		dml_index,
		block_index,
		(ProtobufCMessage *)stmt,
		stmt->returning_list,
		stmt->n_returning_list,
		out_error);
	sqlparser_graph_pop_scope(build);
	return result_status;
}

static int sqlparser_graph_build_update_dml(
	sqlparser_graph_build_t *build,
	PgQuery__UpdateStmt *stmt,
	sqlparser_error_t *out_error)
{
	sqlparser_graph_scope_t scope;
	sqlparser_graph_dml_t dml;
	size_t dml_index;
	size_t block_index;
	size_t relation_index;
	size_t assignment_relation_index;
	size_t fallback_relation_index;
	size_t target_relation_ordinal;
	size_t unique_target_relation_index;
	size_t index;
	int assignment_has_relation;
	int assignment_fallback_allowed;
	int has_fallback_relation;
	int has_unique_target_relation;
	int multi_relation_update;
	int target_relation_ambiguous;

	if (build == NULL || stmt == NULL) {
		return 0;
	}
	memset(&dml, 0, sizeof(dml));
	dml.kind = SQLPARSER_GRAPH_DML_UPDATE;
	if (sqlparser_graph_add_block(build, SQLPARSER_GRAPH_BLOCK_SELECT, &block_index, out_error) != 0) {
		return -1;
	}
	sqlparser_graph_push_scope(
		build,
		&scope,
		block_index,
		stmt->with_clause);
	if (sqlparser_graph_add_dml_target_relation(
		    build,
		    stmt->relation,
		    block_index,
		    &relation_index,
		    out_error) != 0) {
		sqlparser_graph_pop_scope(build);
		return -1;
	}
	dml.target_relation_index = relation_index;
	dml.has_target_relation = stmt->relation != NULL;
	fallback_relation_index = relation_index;
	has_fallback_relation = dml.has_target_relation;
	target_relation_ordinal = 0U;
	assignment_fallback_allowed = 0;
	multi_relation_update =
		build->handle != NULL &&
		sqlparser_dialect_is_mysql_compatible(build->handle->dialect) &&
		sqlparser_mysql_statement_update_join_multi_target(
			build->handle->dialect_state,
			build->statement_index);
	if (multi_relation_update &&
	    sqlparser_mysql_statement_update_join_assignment_fallback(
		    build->handle->dialect_state,
		    build->statement_index)) {
		assignment_fallback_allowed = 1;
	}
	if (build->handle != NULL &&
	    build->handle->dialect == SQLPARSER_DIALECT_DAMENG &&
	    sqlparser_dameng_statement_multi_update_target_index(
		    build->handle->dialect_state,
		    build->statement_index,
		    &target_relation_ordinal)) {
		multi_relation_update = 1;
		assignment_fallback_allowed = 1;
	}
	if (multi_relation_update) {
		dml.has_target_relation = 0;
	}
	if (sqlparser_graph_add_dml(build, &dml, (ProtobufCMessage *)stmt, &dml_index, out_error) != 0) {
		sqlparser_graph_pop_scope(build);
		return -1;
	}
	for (index = 0U; index < stmt->n_from_clause; index++) {
		if (sqlparser_graph_build_from_item(build, block_index, stmt->from_clause[index], out_error) != 0) {
			sqlparser_graph_pop_scope(build);
			return -1;
		}
	}
	if (multi_relation_update && target_relation_ordinal > 0U) {
		size_t direct_relation_ordinal;
		int target_relation_found;

		direct_relation_ordinal = 0U;
		target_relation_found = 0;
		for (index = 0U;
		     index < sqlparser_graph_local_relation_count(build);
		     index++) {
			sqlparser_graph_relation_t *relation;

			relation = sqlparser_graph_relation_by_local(build, index);
			if (relation == NULL || relation->block_index != block_index) {
				continue;
			}
			if (direct_relation_ordinal == target_relation_ordinal) {
				fallback_relation_index = index;
				has_fallback_relation = 1;
				target_relation_found = 1;
				break;
			}
			direct_relation_ordinal++;
		}
		if (!target_relation_found) {
			sqlparser_graph_pop_scope(build);
			sqlparser_error_set_message(
				out_error,
				SQLPARSER_STATUS_INTERNAL_ERROR,
				"multi-table UPDATE target relation is invalid");
			return -1;
		}
	}
	has_unique_target_relation = 0;
	target_relation_ambiguous = 0;
	unique_target_relation_index = 0U;
	for (index = 0U; index < stmt->n_target_list; index++) {
		sqlparser_selector_t selector;

		memset(&selector, 0, sizeof(selector));
		selector.kind = SQLPARSER_SELECTOR_KIND_ASSIGNMENT;
		selector.statement_index = build->statement_index;
		selector.item_index = index;
		if (build->statement_node == NULL ||
		    build->statement_node->node_case !=
			    PG_QUERY__NODE__NODE_UPDATE_STMT ||
		    build->statement_node->update_stmt != stmt) {
			selector.row_index = dml_index + 1U;
		}
		assignment_relation_index = fallback_relation_index;
		assignment_has_relation = multi_relation_update ?
			assignment_fallback_allowed && has_fallback_relation :
			has_fallback_relation;
		if (multi_relation_update) {
			PgQuery__Node *assignment_node;
			PgQuery__ResTarget *assignment_target;

			assignment_node = stmt->target_list != NULL ?
				stmt->target_list[index] : NULL;
			assignment_target = assignment_node != NULL &&
				assignment_node->node_case ==
					PG_QUERY__NODE__NODE_RES_TARGET ?
					assignment_node->res_target : NULL;
			sqlparser_graph_resolve_assignment_relation(
				build,
				block_index,
				assignment_target,
				fallback_relation_index,
				assignment_fallback_allowed &&
					has_fallback_relation,
				&assignment_relation_index,
				&assignment_has_relation);
			if (!assignment_has_relation) {
				target_relation_ambiguous = 1;
			} else if (!has_unique_target_relation) {
				unique_target_relation_index =
					assignment_relation_index;
				has_unique_target_relation = 1;
			} else if (unique_target_relation_index !=
				   assignment_relation_index) {
				target_relation_ambiguous = 1;
			}
		}
		if (sqlparser_graph_add_dml_assignment_from_res_target(
			    build,
			    dml_index,
			    block_index,
			    assignment_relation_index,
			    assignment_has_relation,
			    stmt->target_list != NULL ? stmt->target_list[index] : NULL,
			    sqlparser_graph_multi_assignment_canonical_source(
				    stmt->target_list,
				    stmt->n_target_list,
				    index),
			    &selector,
			    NULL,
			    out_error) != 0) {
			sqlparser_graph_pop_scope(build);
			return -1;
		}
	}
	if (multi_relation_update) {
		sqlparser_graph_dml_t *dml_item;

		dml_item = &build->cache->dml[
			build->statement->dml_offset + dml_index];
		dml_item->target_relation_index = unique_target_relation_index;
		dml_item->has_target_relation =
			has_unique_target_relation && !target_relation_ambiguous;
	}
	if (sqlparser_graph_walk_predicate_expr(build, block_index, SQLPARSER_CLAUSE_KIND_WHERE, stmt->where_clause, out_error) != 0) {
		sqlparser_graph_pop_scope(build);
		return -1;
	}
	if (build->dml_tail_select != NULL &&
	    sqlparser_graph_walk_node_array(
		    build,
		    block_index,
		    SQLPARSER_CLAUSE_KIND_ORDER_BY,
		    build->dml_tail_select->sort_clause,
		    build->dml_tail_select->n_sort_clause,
		    out_error) != 0) {
		sqlparser_graph_pop_scope(build);
		return -1;
	}
	if (sqlparser_graph_ensure_ctes(
		    build,
		    stmt->with_clause,
		    out_error) != 0 ||
	    sqlparser_graph_build_dml_results(
		    build,
		    dml_index,
		    block_index,
		    (ProtobufCMessage *)stmt,
		    stmt->returning_list,
		    stmt->n_returning_list,
		    out_error) != 0) {
		sqlparser_graph_pop_scope(build);
		return -1;
	}
	sqlparser_graph_pop_scope(build);
	return 0;
}

static int sqlparser_graph_build_delete_dml(
	sqlparser_graph_build_t *build,
	PgQuery__DeleteStmt *stmt,
	sqlparser_error_t *out_error)
{
	sqlparser_graph_scope_t scope;
	const sqlparser_dialect_dml_result_dml_t *inventory_dml;
	sqlparser_dialect_dml_result_dml_t dml_result_info;
	sqlparser_graph_dml_t dml;
	size_t dml_index;
	size_t block_index;
	size_t relation_index;
	size_t index;
	const PgQuery__RangeVar *saved_duplicate_target;
	size_t saved_duplicate_block_index;
	int saved_skip_duplicate_target;

	if (build == NULL || stmt == NULL) {
		return 0;
	}
	memset(&dml, 0, sizeof(dml));
	dml.kind = SQLPARSER_GRAPH_DML_DELETE;
	if (sqlparser_graph_add_block(build, SQLPARSER_GRAPH_BLOCK_SELECT, &block_index, out_error) != 0) {
		return -1;
	}
	sqlparser_graph_push_scope(
		build,
		&scope,
		block_index,
		stmt->with_clause);
	if (sqlparser_graph_add_dml_target_relation(
		    build,
		    stmt->relation,
		    block_index,
		    &relation_index,
		    out_error) != 0) {
		sqlparser_graph_pop_scope(build);
		return -1;
	}
	dml.target_relation_index = relation_index;
	dml.has_target_relation = stmt->relation != NULL;
	if (dml.has_target_relation &&
	    sqlparser_graph_span_append_index(build, &dml.delete_targets, relation_index, out_error) != 0) {
		sqlparser_graph_pop_scope(build);
		return -1;
	}
	if (sqlparser_graph_add_dml(build, &dml, (ProtobufCMessage *)stmt, &dml_index, out_error) != 0) {
		sqlparser_graph_pop_scope(build);
		return -1;
	}
	memset(&dml_result_info, 0, sizeof(dml_result_info));
	inventory_dml = sqlparser_graph_dml_inventory_at(build, dml_index);
	if (inventory_dml != NULL) {
		dml_result_info = *inventory_dml;
	} else if (build->handle != NULL) {
		(void)sqlparser_dialect_dml_result_dml_at(
			build->handle,
			build->statement_index,
			dml_index,
			&dml_result_info);
	}
	saved_duplicate_target = build->duplicate_delete_target;
	saved_duplicate_block_index = build->duplicate_delete_block_index;
	saved_skip_duplicate_target = build->skip_duplicate_delete_target;
	build->duplicate_delete_target = stmt->relation;
	build->duplicate_delete_block_index = block_index;
	build->skip_duplicate_delete_target =
		stmt->relation != NULL && dml_result_info.has_duplicate_target_relation;
	for (index = 0U; index < stmt->n_using_clause; index++) {
		if (sqlparser_graph_build_from_item(build, block_index, stmt->using_clause[index], out_error) != 0) {
			build->duplicate_delete_target = saved_duplicate_target;
			build->duplicate_delete_block_index = saved_duplicate_block_index;
			build->skip_duplicate_delete_target = saved_skip_duplicate_target;
			sqlparser_graph_pop_scope(build);
			return -1;
		}
	}
	build->duplicate_delete_target = saved_duplicate_target;
	build->duplicate_delete_block_index = saved_duplicate_block_index;
	build->skip_duplicate_delete_target = saved_skip_duplicate_target;
	if (sqlparser_graph_walk_predicate_expr(build, block_index, SQLPARSER_CLAUSE_KIND_WHERE, stmt->where_clause, out_error) != 0) {
		sqlparser_graph_pop_scope(build);
		return -1;
	}
	if (build->dml_tail_select != NULL &&
	    sqlparser_graph_walk_node_array(
		    build,
		    block_index,
		    SQLPARSER_CLAUSE_KIND_ORDER_BY,
		    build->dml_tail_select->sort_clause,
		    build->dml_tail_select->n_sort_clause,
		    out_error) != 0) {
		sqlparser_graph_pop_scope(build);
		return -1;
	}
	if (sqlparser_graph_ensure_ctes(
		    build,
		    stmt->with_clause,
		    out_error) != 0 ||
	    sqlparser_graph_build_dml_results(
		    build,
		    dml_index,
		    block_index,
		    (ProtobufCMessage *)stmt,
		    stmt->returning_list,
		    stmt->n_returning_list,
		    out_error) != 0) {
		sqlparser_graph_pop_scope(build);
		return -1;
	}
	sqlparser_graph_pop_scope(build);
	return 0;
}

static sqlparser_graph_merge_action_kind_t sqlparser_graph_merge_action_kind(
	PgQuery__CmdType command_type)
{
	switch (command_type) {
		case PG_QUERY__CMD_TYPE__CMD_INSERT:
			return SQLPARSER_GRAPH_MERGE_ACTION_INSERT;
		case PG_QUERY__CMD_TYPE__CMD_UPDATE:
			return SQLPARSER_GRAPH_MERGE_ACTION_UPDATE;
		case PG_QUERY__CMD_TYPE__CMD_DELETE:
			return SQLPARSER_GRAPH_MERGE_ACTION_DELETE;
		case PG_QUERY__CMD_TYPE__CMD_NOTHING:
			return SQLPARSER_GRAPH_MERGE_ACTION_NOTHING;
		default:
			return SQLPARSER_GRAPH_MERGE_ACTION_UNKNOWN;
	}
}

static sqlparser_graph_merge_match_kind_t sqlparser_graph_merge_match_kind(
	PgQuery__MergeMatchKind match_kind)
{
	switch (match_kind) {
		case PG_QUERY__MERGE_MATCH_KIND__MERGE_WHEN_MATCHED:
			return SQLPARSER_GRAPH_MERGE_MATCH_MATCHED;
		case PG_QUERY__MERGE_MATCH_KIND__MERGE_WHEN_NOT_MATCHED_BY_SOURCE:
			return SQLPARSER_GRAPH_MERGE_MATCH_NOT_MATCHED_BY_SOURCE;
		case PG_QUERY__MERGE_MATCH_KIND__MERGE_WHEN_NOT_MATCHED_BY_TARGET:
			return SQLPARSER_GRAPH_MERGE_MATCH_NOT_MATCHED_BY_TARGET;
		default:
			return SQLPARSER_GRAPH_MERGE_MATCH_UNKNOWN;
	}
}

static int sqlparser_graph_build_merge_dml(
	sqlparser_graph_build_t *build,
	PgQuery__MergeStmt *stmt,
	sqlparser_error_t *out_error)
{
	sqlparser_graph_scope_t scope;
	sqlparser_graph_dml_t dml;
	sqlparser_graph_dml_t *dml_item;
	size_t block_index;
	size_t branch_ordinal;
	size_t dml_index;
	size_t first_branch_index;
	size_t index;
	size_t target_relation_index;
	size_t values_ordinal;

	if (build == NULL || stmt == NULL) {
		return 0;
	}
	memset(&dml, 0, sizeof(dml));
	dml.kind = SQLPARSER_GRAPH_DML_MERGE;
	if (sqlparser_graph_add_block(build, SQLPARSER_GRAPH_BLOCK_SELECT, &block_index, out_error) != 0) {
		return -1;
	}
	sqlparser_graph_push_scope(
		build,
		&scope,
		block_index,
		stmt->with_clause);
	if (sqlparser_graph_add_dml_target_relation(
		    build,
		    stmt->relation,
		    block_index,
		    &target_relation_index,
		    out_error) != 0) {
		sqlparser_graph_pop_scope(build);
		return -1;
	}
	dml.target_relation_index = target_relation_index;
	dml.has_target_relation = stmt->relation != NULL;
	if (sqlparser_graph_add_dml(build, &dml, (ProtobufCMessage *)stmt, &dml_index, out_error) != 0) {
		sqlparser_graph_pop_scope(build);
		return -1;
	}
	if (sqlparser_graph_build_from_item(build, block_index, stmt->source_relation, out_error) != 0 ||
	    sqlparser_graph_walk_predicate_expr(build, block_index, SQLPARSER_CLAUSE_KIND_ON, stmt->join_condition, out_error) != 0) {
		sqlparser_graph_pop_scope(build);
		return -1;
	}
	dml_item = &build->cache->dml[
		build->statement->dml_offset + dml_index];
	first_branch_index =
		build->cache->dml_branch_count -
		build->statement->dml_branch_offset;
	values_ordinal = 0U;
	for (index = 0U; index < stmt->n_merge_when_clauses; index++) {
		PgQuery__Node *when_node;
		PgQuery__MergeWhenClause *when_clause;
		sqlparser_graph_dml_branch_t branch;
		sqlparser_graph_merge_branch_detail_t detail;
		size_t branch_index;

		when_node = stmt->merge_when_clauses != NULL ?
			stmt->merge_when_clauses[index] :
			NULL;
		if (when_node == NULL ||
		    when_node->node_case !=
			    PG_QUERY__NODE__NODE_MERGE_WHEN_CLAUSE ||
		    when_node->merge_when_clause == NULL) {
			continue;
		}
		when_clause = when_node->merge_when_clause;
		if (when_clause->command_type ==
			    PG_QUERY__CMD_TYPE__CMD_INSERT &&
		    build->handle->dialect !=
			    SQLPARSER_DIALECT_POSTGRESQL &&
		    values_ordinal > 0U) {
			sqlparser_error_set_message(
				out_error,
				SQLPARSER_STATUS_UNSUPPORTED,
				"multiple MERGE INSERT actions are not supported for this dialect");
			sqlparser_graph_pop_scope(build);
			return -1;
		}
		if (when_clause->command_type ==
		    PG_QUERY__CMD_TYPE__CMD_INSERT) {
			values_ordinal++;
		}
		memset(&branch, 0, sizeof(branch));
		memset(&detail, 0, sizeof(detail));
		branch.dml_index = dml_index;
		branch.ordinal = index;
		branch.branch_kind = SQLPARSER_GRAPH_DML_BRANCH_WHEN;
		detail.action_kind =
			sqlparser_graph_merge_action_kind(
				when_clause->command_type);
		detail.match_kind =
			sqlparser_graph_merge_match_kind(
				when_clause->match_kind);
		branch.target_relation_index = target_relation_index;
		branch.has_target_relation = dml.has_target_relation;
		if (when_clause->condition != NULL) {
			branch.condition_selector.kind =
				SQLPARSER_SELECTOR_KIND_MERGE_BRANCH_CONDITION;
			branch.condition_selector.statement_index =
				build->statement_index;
			branch.condition_selector.row_index = dml_index;
			branch.condition_selector.item_index = index;
			branch.has_condition_selector = 1;
		}
		if (when_clause->command_type ==
			    PG_QUERY__CMD_TYPE__CMD_UPDATE &&
		    when_clause->delete_condition != NULL) {
			branch.delete_condition_selector.kind =
				SQLPARSER_SELECTOR_KIND_MERGE_DELETE_CONDITION;
			branch.delete_condition_selector.statement_index =
				build->statement_index;
			branch.delete_condition_selector.row_index =
				dml_index;
			branch.delete_condition_selector.item_index = index;
			branch.has_delete_condition_selector = 1;
		}
		if (sqlparser_graph_add_dml_branch(
			    build,
			    &branch,
			    &detail,
			    &branch_index,
			    out_error) != 0 ||
		    sqlparser_graph_span_append_index(
			    build,
			    &dml_item->branches,
			    branch_index,
			    out_error) != 0) {
			sqlparser_graph_pop_scope(build);
			return -1;
		}
	}
	values_ordinal = 0U;
	branch_ordinal = 0U;
	for (index = 0U; index < stmt->n_merge_when_clauses; index++) {
		PgQuery__Node *when_node;
		PgQuery__MergeWhenClause *when_clause;
		size_t branch_index;
		size_t item_index;

		when_node = stmt->merge_when_clauses != NULL ? stmt->merge_when_clauses[index] : NULL;
		if (when_node == NULL ||
		    when_node->node_case != PG_QUERY__NODE__NODE_MERGE_WHEN_CLAUSE ||
		    when_node->merge_when_clause == NULL) {
			continue;
		}
		when_clause = when_node->merge_when_clause;
		branch_index = first_branch_index + branch_ordinal++;
		if (sqlparser_graph_walk_predicate_expr(build, block_index, SQLPARSER_CLAUSE_KIND_WHERE, when_clause->condition, out_error) != 0) {
			sqlparser_graph_pop_scope(build);
			return -1;
		}
		if (when_clause->command_type == PG_QUERY__CMD_TYPE__CMD_UPDATE) {
			for (item_index = 0U; item_index < when_clause->n_target_list; item_index++) {
				sqlparser_selector_t selector;
				size_t assignment_index;

				memset(&selector, 0, sizeof(selector));
				selector.kind = SQLPARSER_SELECTOR_KIND_MERGE_ASSIGNMENT;
				selector.statement_index = build->statement_index;
				selector.row_index = dml_index;
				selector.item_index = index;
				selector.column_index = item_index;
				if (sqlparser_graph_add_dml_assignment_from_res_target(
					    build,
					    dml_index,
					    block_index,
					    target_relation_index,
					    dml.has_target_relation,
					    when_clause->target_list != NULL ? when_clause->target_list[item_index] : NULL,
					    sqlparser_graph_multi_assignment_canonical_source(
						    when_clause->target_list,
						    when_clause->n_target_list,
						    item_index),
					    &selector,
					    &assignment_index,
					    out_error) != 0 ||
				    (assignment_index != (size_t)-1 &&
				     sqlparser_graph_span_append_index(
					     build,
					     &build->cache->merge_branch_details[
						     build->statement->dml_branch_offset +
						     branch_index]
						      .assignments,
					     assignment_index,
					     out_error) != 0)) {
					sqlparser_graph_pop_scope(build);
					return -1;
				}
			}
		} else if (when_clause->command_type == PG_QUERY__CMD_TYPE__CMD_INSERT) {
			for (item_index = 0U; item_index < when_clause->n_target_list; item_index++) {
				sqlparser_selector_t selector;
				size_t column_index;

				memset(&selector, 0, sizeof(selector));
				selector.kind =
					SQLPARSER_SELECTOR_KIND_MERGE_INSERT_COLUMN;
				selector.statement_index = build->statement_index;
				selector.row_index = dml_index;
				selector.item_index = index;
				selector.column_index = item_index;

				if (sqlparser_graph_add_dml_column_from_res_target(
					    build,
					    dml_index,
					    item_index,
					    when_clause->target_list != NULL ? when_clause->target_list[item_index] : NULL,
					    &selector,
					    &column_index,
					    out_error) != 0 ||
				    (column_index != (size_t)-1 &&
				     sqlparser_graph_span_append_index(
					     build,
					     &build->cache->dml_branches[
						     build->statement->dml_branch_offset +
						     branch_index]
						      .target_columns,
					     column_index,
					     out_error) != 0)) {
					sqlparser_graph_pop_scope(build);
					return -1;
				}
			}
			for (item_index = 0U; item_index < when_clause->n_values; item_index++) {
				sqlparser_selector_t selector;
				size_t cell_index;

				memset(&selector, 0, sizeof(selector));
				selector.kind =
					SQLPARSER_SELECTOR_KIND_MERGE_INSERT_CELL;
				selector.statement_index = build->statement_index;
				selector.row_index = dml_index;
				selector.item_index = index;
				selector.column_index = item_index;

				if (sqlparser_graph_add_dml_cell_from_node(
					    build,
					    dml_index,
					    block_index,
					    index,
					    0U,
					    item_index,
					    when_clause->values != NULL ? when_clause->values[item_index] : NULL,
					    values_ordinal,
					    &selector,
					    &cell_index,
					    out_error) != 0 ||
				    (cell_index != (size_t)-1 &&
				     sqlparser_graph_span_append_index(
					     build,
					     &build->cache->dml_branches[
						     build->statement->dml_branch_offset +
						     branch_index]
						      .rows,
					     cell_index,
					     out_error) != 0)) {
					sqlparser_graph_pop_scope(build);
					return -1;
				}
			}
			values_ordinal++;
		}
		if (when_clause->delete_condition != NULL &&
		    sqlparser_graph_walk_predicate_expr(
			    build,
			    block_index,
			    SQLPARSER_CLAUSE_KIND_WHERE,
			    when_clause->delete_condition,
			    out_error) != 0) {
			sqlparser_graph_pop_scope(build);
			return -1;
		}
	}
	if (sqlparser_graph_ensure_ctes(
		    build,
		    stmt->with_clause,
		    out_error) != 0 ||
	    sqlparser_graph_build_dml_results(
		    build,
		    dml_index,
		    block_index,
		    (ProtobufCMessage *)stmt,
		    stmt->returning_list,
		    stmt->n_returning_list,
		    out_error) != 0) {
		sqlparser_graph_pop_scope(build);
		return -1;
	}
	sqlparser_graph_pop_scope(build);
	return 0;
}

static int sqlparser_graph_link_nested_dml_source(
	sqlparser_graph_build_t *build,
	size_t dml_index,
	const char *source_name,
	sqlparser_error_t *out_error)
{
	sqlparser_graph_dml_result_cache_t *result_cache;
	sqlparser_graph_dml_result_meta_t *meta;
	size_t global_dml_index;
	size_t source_block_index;
	size_t index;
	int matched;

	if (build == NULL || source_name == NULL || source_name[0] == '\0' ||
	    build->cache->dml_results == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "nested DML result source metadata is missing");
		return -1;
	}
	result_cache = build->cache->dml_results;
	global_dml_index = build->statement->dml_offset + dml_index;
	meta = global_dml_index < result_cache->meta_count &&
		result_cache->metas[global_dml_index].dml_global_index ==
			global_dml_index ?
		&result_cache->metas[global_dml_index] : NULL;
	if (meta == NULL || meta->result_count != 1U ||
	    result_cache->results[meta->result_offset].kind != SQLPARSER_GRAPH_DML_RESULT_CLIENT) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "nested DML must expose one client result");
		return -1;
	}
	source_block_index = result_cache->results[meta->result_offset].block_index;
	matched = 0;
	for (index = 0U; index < sqlparser_graph_local_relation_count(build); index++) {
		sqlparser_graph_relation_identifier_t *identifiers;
		sqlparser_graph_relation_t *relation;
		sqlparser_identifier_source_t relation_source;

		relation = sqlparser_graph_relation_by_local(build, index);
		if (relation == NULL) {
			continue;
		}
		memset(&relation_source, 0, sizeof(relation_source));
		identifiers = sqlparser_graph_relation_identifier_by_local(
			build,
			index);
		if (identifiers != NULL) {
			relation_source = identifiers->object;
		}
		if (relation->object_name == NULL ||
		    !sqlparser_identifier_semantic_equal(
			    build->handle,
			    relation->object_name,
			    relation_source,
			    source_name,
			    relation_source)) {
			continue;
		}
		relation->kind = SQLPARSER_GRAPH_REL_DERIVED;
		relation->database_name = NULL;
		relation->schema_name = NULL;
		relation->object_name = NULL;
		relation->quoted_identifier = 0;
		relation->source_block_index = source_block_index;
		relation->has_source_block = 1;
		relation->has_selector = 0;
		memset(&relation->selector, 0, sizeof(relation->selector));
		matched++;
	}
	if (matched != 1) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "nested DML source relation is missing or ambiguous");
		return -1;
	}
	return 0;
}

static int sqlparser_graph_build_nested_dmls(
	sqlparser_graph_build_t *build,
	sqlparser_error_t *out_error)
{
	size_t dml_count;
	size_t dml_index;

	dml_count = sqlparser_dialect_dml_result_count(
		build->handle,
		build->statement_index);
	for (dml_index = 1U; dml_index < dml_count; dml_index++) {
		sqlparser_dialect_dml_result_dml_t dml_info;
		const char *saved_dml_source_sql;
		ProtobufCMessage *message;
		sqlparser_graph_dml_kind_t kind;
		sqlparser_status_t status;
		int build_status;

		memset(&dml_info, 0, sizeof(dml_info));
		if (!sqlparser_dialect_dml_result_dml_at(
			    build->handle,
			    build->statement_index,
			    dml_index,
			    &dml_info) ||
		    !dml_info.has_parent) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "nested DML metadata is inconsistent");
			return -1;
		}
		message = NULL;
		kind = (sqlparser_graph_dml_kind_t)0;
		status = sqlparser_get_dml_result_message(
			build->handle,
			build->statement_index,
			dml_index,
			&kind,
			&message,
			out_error);
		if (status != SQLPARSER_STATUS_OK || kind != dml_info.kind || message == NULL) {
			return -1;
		}
		saved_dml_source_sql = build->dml_source_sql;
		build->dml_source_sql = dml_info.source_sql;
		switch (kind) {
			case SQLPARSER_GRAPH_DML_INSERT:
				build_status = sqlparser_graph_build_insert_dml(build, (PgQuery__InsertStmt *)message, out_error);
				break;
			case SQLPARSER_GRAPH_DML_UPDATE:
				build_status = sqlparser_graph_build_update_dml(build, (PgQuery__UpdateStmt *)message, out_error);
				break;
			case SQLPARSER_GRAPH_DML_DELETE:
				build_status = sqlparser_graph_build_delete_dml(build, (PgQuery__DeleteStmt *)message, out_error);
				break;
			case SQLPARSER_GRAPH_DML_MERGE:
				build_status = sqlparser_graph_build_merge_dml(build, (PgQuery__MergeStmt *)message, out_error);
				break;
			default:
				sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "nested DML kind is invalid");
				build->dml_source_sql = saved_dml_source_sql;
				return -1;
		}
		build->dml_source_sql = saved_dml_source_sql;
		if (build_status != 0 ||
		    sqlparser_graph_link_nested_dml_source(
			    build, dml_index, dml_info.source_name, out_error) != 0) {
			return -1;
		}
	}
	return 0;
}

static int sqlparser_graph_build_dml_statement(
	sqlparser_graph_build_t *build,
	PgQuery__Node *statement,
	sqlparser_error_t *out_error)
{
	int status;

	if (build->dml_inventory_count > 0U) {
		if (build->has_claiming_dml_index) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "PostgreSQL DML inventory claim is already active");
			return -1;
		}
		build->claiming_dml_index = 0U;
		build->has_claiming_dml_index = 1;
	}

	switch (statement->node_case) {
		case PG_QUERY__NODE__NODE_INSERT_STMT:
			if (build->handle != NULL &&
			    sqlparser_dialect_state_has_multi_insert(build->handle->dialect, build->handle->dialect_state)) {
				status = sqlparser_graph_build_multi_insert_dml(
					build,
					statement->insert_stmt,
					sqlparser_dialect_state_multi_insert(build->handle->dialect, build->handle->dialect_state),
					out_error);
			} else {
				status = sqlparser_graph_build_insert_dml(build, statement->insert_stmt, out_error);
			}
			break;
		case PG_QUERY__NODE__NODE_UPDATE_STMT:
			status = sqlparser_graph_build_update_dml(build, statement->update_stmt, out_error);
			break;
		case PG_QUERY__NODE__NODE_DELETE_STMT:
			status = sqlparser_graph_build_delete_dml(build, statement->delete_stmt, out_error);
			break;
		case PG_QUERY__NODE__NODE_MERGE_STMT:
			status = sqlparser_graph_build_merge_dml(build, statement->merge_stmt, out_error);
			break;
		default:
			return 0;
	}
	return status == 0 && build->handle != NULL &&
		sqlparser_dialect_is_sqlserver_compatible(build->handle->dialect) ?
		sqlparser_graph_build_nested_dmls(build, out_error) : status;
}

static int sqlparser_graph_build_statement(
	sqlparser_graph_build_t *build,
	PgQuery__Node *statement,
	sqlparser_error_t *out_error)
{
	sqlparser_graph_scope_t scope;
	size_t block_index;

	if (statement == NULL) {
		return 0;
	}
	if (build != NULL && sqlparser_control_unit_is_condition(build->handle, build->statement_index)) {
		PgQuery__Node *condition;

		condition = statement;
		if (statement->node_case == PG_QUERY__NODE__NODE_SELECT_STMT &&
		    statement->select_stmt != NULL) {
			condition = statement->select_stmt->where_clause;
		}
		if (condition == NULL) {
			sqlparser_error_set_message(
				out_error,
				SQLPARSER_STATUS_INTERNAL_ERROR,
				"control condition AST is missing");
			return -1;
		}
		if (sqlparser_graph_add_block(build, SQLPARSER_GRAPH_BLOCK_CONDITION, &block_index, out_error) != 0) {
			return -1;
		}
		sqlparser_graph_push_scope(build, &scope, block_index, NULL);
		if (sqlparser_graph_walk_predicate_expr(
			    build,
			    block_index,
			    SQLPARSER_CLAUSE_KIND_CONDITION,
			    condition,
			    out_error) != 0) {
			sqlparser_graph_pop_scope(build);
			return -1;
		}
		sqlparser_graph_pop_scope(build);
		return 0;
	}
	if (sqlparser_graph_build_session_statement(build, statement, out_error) != 0) {
		return -1;
	}
	switch (statement->node_case) {
		case PG_QUERY__NODE__NODE_SELECT_STMT:
			return sqlparser_graph_build_select(build, statement->select_stmt, SQLPARSER_GRAPH_BLOCK_SELECT, NULL, out_error);
		case PG_QUERY__NODE__NODE_VIEW_STMT:
			return statement->view_stmt != NULL &&
					statement->view_stmt->query != NULL &&
					statement->view_stmt->query->node_case == PG_QUERY__NODE__NODE_SELECT_STMT ?
				sqlparser_graph_build_select(build, statement->view_stmt->query->select_stmt, SQLPARSER_GRAPH_BLOCK_SELECT, NULL, out_error) :
				0;
		case PG_QUERY__NODE__NODE_CREATE_TABLE_AS_STMT:
			return statement->create_table_as_stmt != NULL &&
					statement->create_table_as_stmt->query != NULL &&
					statement->create_table_as_stmt->query->node_case == PG_QUERY__NODE__NODE_SELECT_STMT ?
				sqlparser_graph_build_select(build, statement->create_table_as_stmt->query->select_stmt, SQLPARSER_GRAPH_BLOCK_SELECT, NULL, out_error) :
				0;
		case PG_QUERY__NODE__NODE_INSERT_STMT:
		case PG_QUERY__NODE__NODE_UPDATE_STMT:
		case PG_QUERY__NODE__NODE_DELETE_STMT:
		case PG_QUERY__NODE__NODE_MERGE_STMT:
			return sqlparser_graph_build_dml_statement(build, statement, out_error);
		default:
			return 0;
		}
	}

static sqlparser_status_t sqlparser_query_graph_cache_build(
	sqlparser_handle_t *handle,
	sqlparser_query_graph_cache_t **out_cache,
	sqlparser_error_t *out_error)
{
	const sqlparser_identifier_origin_map_t *identifier_origins;
	sqlparser_query_graph_cache_t *cache;
	sqlparser_view_bind_position_cache_t bind_positions;
	size_t statement_index;
	sqlparser_status_t status;

	if (out_cache == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "out_cache must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_cache = NULL;
	if (handle == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "handle must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	memset(&bind_positions, 0, sizeof(bind_positions));
	status = sqlparser_handle_ensure_ast(handle, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	if (handle->sql_len != handle->parser_sql_len ||
	    (handle->sql_len > 0U &&
	     memcmp(handle->sql, handle->parser_sql, handle->sql_len) != 0)) {
		status = sqlparser_identifier_origins_for_handle(
			handle,
			&identifier_origins,
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
	}
	cache = (sqlparser_query_graph_cache_t *)calloc(1U, sizeof(*cache));
	if (cache == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	cache->generation = handle->generation;
	cache->statement_count = handle->statement_count;
	if (cache->statement_count > 0U) {
		cache->statements = (sqlparser_statement_graph_t *)calloc(cache->statement_count, sizeof(*cache->statements));
		if (cache->statements == NULL) {
			sqlparser_query_graph_cache_release(cache);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
	}
	for (statement_index = 0U; statement_index < handle->statement_count; statement_index++) {
		PgQuery__Node *statement_node;
		sqlparser_graph_build_t build;

		statement_node = NULL;
		status = sqlparser_get_statement_node(handle, statement_index, &statement_node, out_error);
		if (status != SQLPARSER_STATUS_OK) {
			sqlparser_view_bind_position_cache_release(&bind_positions);
			sqlparser_query_graph_cache_release(cache);
			return status;
		}
		memset(&build, 0, sizeof(build));
		build.handle = handle;
		build.cache = cache;
		build.bind_positions = &bind_positions;
		build.statement = &cache->statements[statement_index];
		build.statement_index = statement_index;
		build.statement_node = statement_node;
		status = sqlparser_get_mysql_dml_tail_select(
			handle,
			statement_index,
			statement_node,
			&build.dml_tail_select,
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			sqlparser_view_bind_position_cache_release(&bind_positions);
			sqlparser_query_graph_cache_release(cache);
			return status;
		}
		build.statement->block_offset = cache->block_count;
		build.statement->relation_offset = cache->relation_count;
		build.statement->target_offset = cache->target_count;
		build.statement->field_offset = cache->field_count;
		build.statement->value_offset = cache->value_count;
		build.statement->set_offset = cache->set_count;
		build.statement->predicate_offset = cache->predicate_count;
		build.statement->dml_offset = cache->dml_count;
		build.statement->dml_branch_offset = cache->dml_branch_count;
		build.statement->dml_column_offset = cache->dml_column_count;
		build.statement->dml_cell_offset = cache->dml_cell_count;
		build.statement->dml_assignment_offset = cache->dml_assignment_count;
		build.statement->dml_reference_offset = cache->dml_results != NULL ?
			cache->dml_results->reference_count : 0U;
		build.statement->session_item_offset = cache->session_item_count;
		build.statement->session_value_offset = cache->session_value_count;
		if (sqlparser_graph_prepare_dml_inventory(&build, out_error) != 0 ||
		    sqlparser_graph_build_statement(&build, statement_node, out_error) != 0 ||
		    sqlparser_graph_finalize_statement_spans(&build, out_error) != 0) {
			sqlparser_graph_build_clear(&build);
			sqlparser_view_bind_position_cache_release(&bind_positions);
			sqlparser_query_graph_cache_release(cache);
			if (out_error != NULL && out_error->code == SQLPARSER_STATUS_OK) {
				sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "failed to build query graph");
			}
			return out_error != NULL ? out_error->code : SQLPARSER_STATUS_INTERNAL_ERROR;
		}
		sqlparser_graph_build_clear(&build);
	}
	sqlparser_view_bind_position_cache_release(&bind_positions);
	sqlparser_query_graph_shrink_array(
		(void **)&cache->targets,
		&cache->target_capacity,
		cache->target_count,
		sizeof(*cache->targets));
	sqlparser_query_graph_shrink_array(
		(void **)&cache->values,
		&cache->value_capacity,
		cache->value_count,
		sizeof(*cache->values));
	sqlparser_query_graph_shrink_array(
		(void **)&cache->value_text,
		&cache->value_text_capacity,
		cache->value_text_length,
		sizeof(*cache->value_text));
	sqlparser_query_graph_shrink_array(
		(void **)&cache->like_escapes,
		&cache->like_escape_capacity,
		cache->like_escape_count,
		sizeof(*cache->like_escapes));
	sqlparser_query_graph_shrink_array(
		(void **)&cache->dml_cells,
		&cache->dml_cell_capacity,
		cache->dml_cell_count,
		sizeof(*cache->dml_cells));
	sqlparser_query_graph_shrink_array(
		(void **)&cache->index_pool,
		&cache->index_pool_capacity,
		cache->index_pool_count,
		sizeof(*cache->index_pool));
	*out_cache = cache;
	return SQLPARSER_STATUS_OK;
}

void sqlparser_relation_bindings_clear(
	sqlparser_relation_bindings_t *bindings)
{
	if (bindings == NULL) {
		return;
	}
	free(bindings->column_refs);
	free(bindings->assignment_targets);
	memset(bindings, 0, sizeof(*bindings));
}

sqlparser_status_t sqlparser_collect_relation_bindings(
	sqlparser_handle_t *handle,
	size_t statement_index,
	PgQuery__RangeVar *relation,
	sqlparser_relation_bindings_t *out_bindings,
	sqlparser_error_t *out_error)
{
	sqlparser_query_graph_cache_t *cache;
	sqlparser_view_bind_position_cache_t bind_positions;
	sqlparser_graph_build_t build;
	PgQuery__Node *statement_node;
	sqlparser_status_t status;

	sqlparser_error_clear(out_error);
	if (relation == NULL || out_bindings == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"relation and binding outputs must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	memset(out_bindings, 0, sizeof(*out_bindings));
	if (handle == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"handle must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	status = sqlparser_handle_ensure_ast(handle, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	if (statement_index >= handle->statement_count) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"statement_index is out of range");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	cache = (sqlparser_query_graph_cache_t *)calloc(1U, sizeof(*cache));
	if (cache == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_NO_MEMORY,
			"out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	cache->generation = handle->generation;
	cache->statement_count = 1U;
	cache->statements =
		(sqlparser_statement_graph_t *)calloc(
			1U,
			sizeof(*cache->statements));
	if (cache->statements == NULL) {
		sqlparser_query_graph_cache_release(cache);
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_NO_MEMORY,
			"out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	memset(&bind_positions, 0, sizeof(bind_positions));
	memset(&build, 0, sizeof(build));
	statement_node = NULL;
	status = sqlparser_get_statement_node(
		handle,
		statement_index,
		&statement_node,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		goto fail;
	}
	build.handle = handle;
	build.cache = cache;
	build.bind_positions = &bind_positions;
	build.statement = &cache->statements[0];
	build.statement_index = statement_index;
	build.statement_node = statement_node;
	build.collect_relation_bindings = 1;
	build.collect_relation = relation;
	status = sqlparser_get_mysql_dml_tail_select(
		handle,
		statement_index,
		statement_node,
		&build.dml_tail_select,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		goto fail;
	}
	build.statement->block_offset = cache->block_count;
	build.statement->relation_offset = cache->relation_count;
	build.statement->target_offset = cache->target_count;
	build.statement->field_offset = cache->field_count;
	build.statement->value_offset = cache->value_count;
	build.statement->set_offset = cache->set_count;
	build.statement->predicate_offset = cache->predicate_count;
	build.statement->dml_offset = cache->dml_count;
	build.statement->dml_branch_offset = cache->dml_branch_count;
	build.statement->dml_column_offset = cache->dml_column_count;
	build.statement->dml_cell_offset = cache->dml_cell_count;
	build.statement->dml_assignment_offset =
		cache->dml_assignment_count;
	build.statement->dml_reference_offset = 0U;
	build.statement->session_item_offset = cache->session_item_count;
	build.statement->session_value_offset = cache->session_value_count;
	if (sqlparser_graph_prepare_dml_inventory(
		    &build,
		    out_error) != 0 ||
	    sqlparser_graph_build_statement(
		    &build,
		    statement_node,
		    out_error) != 0 ||
	    sqlparser_graph_finalize_statement_spans(
		    &build,
		    out_error) != 0) {
		if (out_error != NULL &&
		    out_error->code != SQLPARSER_STATUS_OK) {
			status = out_error->code;
		} else {
			status = SQLPARSER_STATUS_INTERNAL_ERROR;
			sqlparser_error_set_message(
				out_error,
				status,
				"failed to collect relation bindings");
		}
		goto fail;
	}
	if (build.collect_target_relation_ambiguous) {
		status = SQLPARSER_STATUS_INTERNAL_ERROR;
		sqlparser_error_set_message(
			out_error,
			status,
			"relation selector resolved to multiple relations");
		goto fail;
	}
	out_bindings->column_refs = build.collected_column_refs;
	out_bindings->column_ref_count = build.collected_column_ref_count;
	out_bindings->assignment_targets = build.collected_assignment_targets;
	out_bindings->assignment_target_count =
		build.collected_assignment_target_count;
	build.collected_column_refs = NULL;
	build.collected_column_ref_count = 0U;
	build.collected_column_ref_capacity = 0U;
	build.collected_assignment_targets = NULL;
	build.collected_assignment_target_count = 0U;
	build.collected_assignment_target_capacity = 0U;
	sqlparser_graph_build_clear(&build);
	sqlparser_view_bind_position_cache_release(&bind_positions);
	sqlparser_query_graph_cache_release(cache);
	return SQLPARSER_STATUS_OK;

fail:
	sqlparser_graph_build_clear(&build);
	sqlparser_view_bind_position_cache_release(&bind_positions);
	sqlparser_query_graph_cache_release(cache);
	return status;
}

static sqlparser_status_t sqlparser_query_graph_ensure(
	sqlparser_handle_t *handle,
	sqlparser_error_t *out_error)
{
	sqlparser_query_graph_cache_t *cache;
	sqlparser_status_t status;

	if (handle == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "handle must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	if (handle->query_graph != NULL &&
	    handle->query_graph_generation == handle->generation &&
	    handle->query_graph->generation == handle->generation) {
		return SQLPARSER_STATUS_OK;
	}
	sqlparser_handle_clear_query_graph(handle);
	cache = NULL;
	status = sqlparser_query_graph_cache_build(handle, &cache, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	handle->query_graph = cache;
	handle->query_graph_generation = handle->generation;
	return SQLPARSER_STATUS_OK;
}

static sqlparser_statement_graph_t *sqlparser_query_graph_statement(
	const sqlparser_query_graph_view_t *graph,
	sqlparser_query_graph_cache_t **out_cache)
{
	sqlparser_handle_t *handle;
	sqlparser_query_graph_cache_t *cache;

	if (out_cache != NULL) {
		*out_cache = NULL;
	}
	if (graph == NULL || graph->handle == NULL) {
		return NULL;
	}
	handle = (sqlparser_handle_t *)graph->handle;
	cache = handle->query_graph;
	if (cache == NULL || graph->generation != handle->generation ||
	    cache->generation != graph->generation ||
	    graph->statement_index >= cache->statement_count) {
		return NULL;
	}
	if (out_cache != NULL) {
		*out_cache = cache;
	}
	return &cache->statements[graph->statement_index];
}

sqlparser_status_t sqlparser_statement_query_graph(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	sqlparser_query_graph_view_t *out_graph,
	sqlparser_error_t *out_error)
{
	sqlparser_handle_t *mutable_handle;
	sqlparser_statement_graph_t *statement;
	sqlparser_status_t status;

	sqlparser_error_clear(out_error);
	if (out_graph == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "out_graph must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	memset(out_graph, 0, sizeof(*out_graph));
	if (handle == NULL || statement_index >= handle->statement_count) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "statement_index is out of range");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	mutable_handle = (sqlparser_handle_t *)handle;
	status = sqlparser_query_graph_ensure(mutable_handle, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	statement = &mutable_handle->query_graph->statements[statement_index];
	out_graph->handle = handle;
	out_graph->statement_index = statement_index;
	out_graph->generation = handle->generation;
	out_graph->root_block_index = statement->root_block_index;
	out_graph->has_root_block = statement->has_root_block;
	out_graph->block_count = statement->block_count;
	out_graph->relation_count = statement->relation_count;
	out_graph->target_count = statement->target_count;
	out_graph->field_count = statement->field_count;
	out_graph->value_count = statement->value_count;
	out_graph->set_count = statement->set_count;
	out_graph->predicate_count = statement->predicate_count;
	out_graph->has_dml = statement->dml_count > 0U;
	out_graph->dml_branch_count = statement->dml_branch_count;
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_query_graph_span_index_at(
	const sqlparser_query_graph_view_t *graph,
	sqlparser_index_span_t span,
	size_t item_index,
	size_t *out_index,
	sqlparser_error_t *out_error)
{
	sqlparser_query_graph_cache_t *cache;

	sqlparser_error_clear(out_error);
	if (out_index == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "out_index must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_index = 0U;
	(void)sqlparser_query_graph_statement(graph, &cache);
	if (cache == NULL || item_index >= span.count ||
	    span.offset > cache->index_pool_count ||
	    item_index >= cache->index_pool_count - span.offset) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "span index is out of range");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_index = cache->index_pool[span.offset + item_index];
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_query_graph_block_at(
	const sqlparser_query_graph_view_t *graph,
	size_t block_index,
	sqlparser_graph_block_t *out_block,
	sqlparser_error_t *out_error)
{
	sqlparser_query_graph_cache_t *cache;
	sqlparser_statement_graph_t *statement;

	sqlparser_error_clear(out_error);
	if (out_block == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "out_block must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	memset(out_block, 0, sizeof(*out_block));
	statement = sqlparser_query_graph_statement(graph, &cache);
	if (statement == NULL || block_index >= statement->block_count) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "block_index is out of range");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_block = cache->blocks[statement->block_offset + block_index];
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_query_graph_relation_at(
	const sqlparser_query_graph_view_t *graph,
	size_t relation_index,
	sqlparser_graph_relation_t *out_relation,
	sqlparser_error_t *out_error)
{
	sqlparser_query_graph_cache_t *cache;
	sqlparser_statement_graph_t *statement;

	sqlparser_error_clear(out_error);
	if (out_relation == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "out_relation must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	memset(out_relation, 0, sizeof(*out_relation));
	statement = sqlparser_query_graph_statement(graph, &cache);
	if (statement == NULL || relation_index >= statement->relation_count) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "relation_index is out of range");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_relation = cache->relations[statement->relation_offset + relation_index];
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_query_graph_target_at(
	const sqlparser_query_graph_view_t *graph,
	size_t target_index,
	sqlparser_graph_target_t *out_target,
	sqlparser_error_t *out_error)
{
	sqlparser_query_graph_cache_t *cache;
	sqlparser_statement_graph_t *statement;

	sqlparser_error_clear(out_error);
	if (out_target == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "out_target must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	memset(out_target, 0, sizeof(*out_target));
	statement = sqlparser_query_graph_statement(graph, &cache);
	if (statement == NULL || target_index >= statement->target_count) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "target_index is out of range");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	sqlparser_graph_target_cache_copy_public(
		&cache->targets[statement->target_offset + target_index],
		graph->statement_index,
		target_index,
		out_target);
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_query_graph_field_at(
	const sqlparser_query_graph_view_t *graph,
	size_t field_index,
	sqlparser_graph_field_t *out_field,
	sqlparser_error_t *out_error)
{
	sqlparser_query_graph_cache_t *cache;
	sqlparser_statement_graph_t *statement;

	sqlparser_error_clear(out_error);
	if (out_field == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "out_field must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	memset(out_field, 0, sizeof(*out_field));
	statement = sqlparser_query_graph_statement(graph, &cache);
	if (statement == NULL || field_index >= statement->field_count) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "field_index is out of range");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_field = cache->fields[statement->field_offset + field_index];
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_query_graph_value_at(
	const sqlparser_query_graph_view_t *graph,
	size_t value_index,
	sqlparser_graph_value_t *out_value,
	sqlparser_error_t *out_error)
{
	sqlparser_query_graph_cache_t *cache;
	sqlparser_statement_graph_t *statement;
	sqlparser_graph_value_t value;

	sqlparser_error_clear(out_error);
	if (out_value == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "out_value must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	memset(out_value, 0, sizeof(*out_value));
	statement = sqlparser_query_graph_statement(graph, &cache);
	if (statement == NULL || value_index >= statement->value_count) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "value_index is out of range");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	if (sqlparser_graph_value_cache_copy_public(
		    cache,
		    &cache->values[statement->value_offset + value_index],
		    statement->value_offset + value_index,
		    graph->statement_index,
		    value_index,
		    &value,
		    out_error) != 0) {
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}
	*out_value = value;
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_query_graph_set_at(
	const sqlparser_query_graph_view_t *graph,
	size_t set_index,
	sqlparser_graph_set_t *out_set,
	sqlparser_error_t *out_error)
{
	sqlparser_query_graph_cache_t *cache;
	sqlparser_statement_graph_t *statement;

	sqlparser_error_clear(out_error);
	if (out_set == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "out_set must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	memset(out_set, 0, sizeof(*out_set));
	statement = sqlparser_query_graph_statement(graph, &cache);
	if (statement == NULL || set_index >= statement->set_count) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "set_index is out of range");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_set = cache->sets[statement->set_offset + set_index];
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_query_graph_predicate_at(
	const sqlparser_query_graph_view_t *graph,
	size_t predicate_index,
	sqlparser_graph_predicate_t *out_predicate,
	sqlparser_error_t *out_error)
{
	sqlparser_query_graph_cache_t *cache;
	sqlparser_statement_graph_t *statement;

	sqlparser_error_clear(out_error);
	if (out_predicate == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "out_predicate must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	memset(out_predicate, 0, sizeof(*out_predicate));
	statement = sqlparser_query_graph_statement(graph, &cache);
	if (statement == NULL || predicate_index >= statement->predicate_count) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "predicate_index is out of range");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_predicate = cache->predicates[statement->predicate_offset + predicate_index];
	return SQLPARSER_STATUS_OK;
}

static const char *sqlparser_query_graph_session_text_at(
	const sqlparser_query_graph_cache_t *cache,
	size_t offset)
{
	if (cache == NULL || cache->session_text == NULL ||
	    offset >= cache->session_text_length) {
		return NULL;
	}
	return cache->session_text + offset;
}

sqlparser_status_t sqlparser_query_graph_session(
	const sqlparser_query_graph_view_t *graph,
	sqlparser_graph_session_t *out_session,
	sqlparser_error_t *out_error)
{
	sqlparser_statement_graph_t *statement;

	sqlparser_error_clear(out_error);
	if (out_session == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "out_session must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	memset(out_session, 0, sizeof(*out_session));
	statement = sqlparser_query_graph_statement(graph, NULL);
	if (statement == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "query graph is invalid");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	out_session->action = statement->session_action;
	out_session->item_count = statement->session_item_count;
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_query_graph_session_item_at(
	const sqlparser_query_graph_view_t *graph,
	size_t item_index,
	sqlparser_graph_session_item_t *out_item,
	sqlparser_error_t *out_error)
{
	sqlparser_query_graph_cache_t *cache;
	sqlparser_statement_graph_t *statement;
	const sqlparser_graph_session_item_cache_t *item;

	sqlparser_error_clear(out_error);
	if (out_item == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "out_item must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	memset(out_item, 0, sizeof(*out_item));
	statement = sqlparser_query_graph_statement(graph, &cache);
	if (statement == NULL || item_index >= statement->session_item_count) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "session item index is out of range");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	item = &cache->session_items[statement->session_item_offset + item_index];
	out_item->index = item_index;
	out_item->scope = item->scope;
	out_item->target_kind = item->target_kind;
	out_item->value_count = item->value_count;
	if (item->value_offset < statement->session_value_offset ||
	    item->value_count > statement->session_value_count ||
	    item->value_offset - statement->session_value_offset >
		    statement->session_value_count - item->value_count) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "session item value span is invalid");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}
	out_item->value_offset = item->value_offset - statement->session_value_offset;
	if (item->has_name) {
		out_item->name = sqlparser_query_graph_session_text_at(cache, item->name_offset);
		if (out_item->name == NULL) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "session item name is invalid");
			return SQLPARSER_STATUS_INTERNAL_ERROR;
		}
	}
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_query_graph_session_value_at(
	const sqlparser_query_graph_view_t *graph,
	size_t value_index,
	sqlparser_graph_session_value_t *out_value,
	sqlparser_error_t *out_error)
{
	sqlparser_query_graph_cache_t *cache;
	sqlparser_statement_graph_t *statement;
	const sqlparser_graph_session_value_cache_t *value;

	sqlparser_error_clear(out_error);
	if (out_value == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "out_value must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	memset(out_value, 0, sizeof(*out_value));
	statement = sqlparser_query_graph_statement(graph, &cache);
	if (statement == NULL || value_index >= statement->session_value_count) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "session value index is out of range");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	value = &cache->session_values[statement->session_value_offset + value_index];
	out_value->index = value_index;
	out_value->kind = value->kind;
	out_value->literal.kind = value->literal_kind;
	out_value->literal.integer_value = value->literal_integer;
	out_value->literal.boolean_value = value->literal_boolean;
	out_value->literal.quoted_identifier = value->literal_quoted_identifier;
	out_value->bind_kind = value->bind_kind;
	out_value->bind_position = value->bind_position;
	out_value->has_bind_position = value->has_bind_position;
	if (value->has_name) {
		out_value->name = sqlparser_query_graph_session_text_at(cache, value->name_offset);
	}
	if (value->has_text) {
		out_value->text = sqlparser_query_graph_session_text_at(cache, value->text_offset);
	}
	if (value->has_literal_text) {
		const char *literal_text;

		literal_text = sqlparser_query_graph_session_text_at(cache, value->literal_text_offset);
		if (value->literal_kind == SQLPARSER_LITERAL_KIND_STRING) {
			out_value->literal.string_value = literal_text;
		} else if (value->literal_kind == SQLPARSER_LITERAL_KIND_FLOAT) {
			out_value->literal.float_value = literal_text;
		}
	}
	if (value->has_bind_key) {
		out_value->bind_key = sqlparser_query_graph_session_text_at(cache, value->bind_key_offset);
	}
	if (value->has_bind_sql) {
		out_value->bind_sql = sqlparser_query_graph_session_text_at(cache, value->bind_sql_offset);
	}
	if ((value->has_name && out_value->name == NULL) ||
	    (value->has_text && out_value->text == NULL) ||
	    (value->has_literal_text &&
	     out_value->literal.string_value == NULL &&
	     out_value->literal.float_value == NULL) ||
	    (value->has_bind_key && out_value->bind_key == NULL) ||
	    (value->has_bind_sql && out_value->bind_sql == NULL)) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "session value text is invalid");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_query_graph_dml(
	const sqlparser_query_graph_view_t *graph,
	sqlparser_graph_dml_t *out_dml,
	sqlparser_error_t *out_error)
{
	sqlparser_query_graph_cache_t *cache;
	sqlparser_statement_graph_t *statement;

	sqlparser_error_clear(out_error);
	if (out_dml == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "out_dml must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	memset(out_dml, 0, sizeof(*out_dml));
	statement = sqlparser_query_graph_statement(graph, &cache);
	if (statement == NULL || statement->dml_count == 0U) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "statement has no dml graph");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_dml = cache->dml[statement->dml_offset];
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_query_graph_dml_count(
	const sqlparser_query_graph_view_t *graph,
	size_t *out_count,
	sqlparser_error_t *out_error)
{
	sqlparser_statement_graph_t *statement;

	sqlparser_error_clear(out_error);
	if (out_count == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "out_count must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_count = 0U;
	statement = sqlparser_query_graph_statement(graph, NULL);
	if (statement == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "query graph is invalid");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_count = statement->dml_count;
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_query_graph_dml_at(
	const sqlparser_query_graph_view_t *graph,
	size_t dml_index,
	sqlparser_graph_dml_t *out_dml,
	sqlparser_error_t *out_error)
{
	sqlparser_query_graph_cache_t *cache;
	sqlparser_statement_graph_t *statement;

	sqlparser_error_clear(out_error);
	if (out_dml == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "out_dml must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	memset(out_dml, 0, sizeof(*out_dml));
	statement = sqlparser_query_graph_statement(graph, &cache);
	if (statement == NULL || dml_index >= statement->dml_count) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "dml_index is out of range");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_dml = cache->dml[statement->dml_offset + dml_index];
	return SQLPARSER_STATUS_OK;
}

static sqlparser_graph_dml_result_meta_t *sqlparser_query_graph_dml_result_meta(
	sqlparser_query_graph_cache_t *cache,
	sqlparser_statement_graph_t *statement,
	size_t dml_index)
{
	size_t global_dml_index;

	if (cache == NULL || statement == NULL || cache->dml_results == NULL ||
	    dml_index >= statement->dml_count) {
		return NULL;
	}
	global_dml_index = statement->dml_offset + dml_index;
	return global_dml_index < cache->dml_results->meta_count &&
		cache->dml_results->metas[global_dml_index].dml_global_index ==
			global_dml_index ?
		&cache->dml_results->metas[global_dml_index] : NULL;
}

sqlparser_status_t sqlparser_query_graph_dml_parent(
	const sqlparser_query_graph_view_t *graph,
	size_t dml_index,
	size_t *out_parent_dml_index,
	int *out_has_parent,
	sqlparser_error_t *out_error)
{
	sqlparser_query_graph_cache_t *cache;
	sqlparser_statement_graph_t *statement;
	sqlparser_graph_dml_result_meta_t *meta;

	sqlparser_error_clear(out_error);
	if (out_parent_dml_index == NULL || out_has_parent == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "DML parent outputs must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_parent_dml_index = 0U;
	*out_has_parent = 0;
	statement = sqlparser_query_graph_statement(graph, &cache);
	if (statement == NULL || dml_index >= statement->dml_count) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "dml_index is out of range");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	meta = sqlparser_query_graph_dml_result_meta(cache, statement, dml_index);
	if (meta != NULL && meta->has_parent) {
		*out_parent_dml_index = meta->parent_dml_index;
		*out_has_parent = 1;
	}
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_query_graph_dml_result_count(
	const sqlparser_query_graph_view_t *graph,
	size_t dml_index,
	size_t *out_count,
	sqlparser_error_t *out_error)
{
	sqlparser_query_graph_cache_t *cache;
	sqlparser_statement_graph_t *statement;
	sqlparser_graph_dml_result_meta_t *meta;

	sqlparser_error_clear(out_error);
	if (out_count == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "out_count must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_count = 0U;
	statement = sqlparser_query_graph_statement(graph, &cache);
	if (statement == NULL || dml_index >= statement->dml_count) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "dml_index is out of range");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	meta = sqlparser_query_graph_dml_result_meta(cache, statement, dml_index);
	if (meta != NULL) {
		*out_count = meta->result_count;
	}
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_query_graph_dml_result_at(
	const sqlparser_query_graph_view_t *graph,
	size_t dml_index,
	size_t result_index,
	sqlparser_graph_dml_result_t *out_result,
	sqlparser_error_t *out_error)
{
	sqlparser_query_graph_cache_t *cache;
	sqlparser_statement_graph_t *statement;
	sqlparser_graph_dml_result_meta_t *meta;

	sqlparser_error_clear(out_error);
	if (out_result == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "out_result must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	memset(out_result, 0, sizeof(*out_result));
	statement = sqlparser_query_graph_statement(graph, &cache);
	if (statement == NULL || dml_index >= statement->dml_count) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "dml_index is out of range");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	meta = sqlparser_query_graph_dml_result_meta(cache, statement, dml_index);
	if (meta == NULL || result_index >= meta->result_count) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "DML result index is out of range");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_result = cache->dml_results->results[meta->result_offset + result_index];
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_query_graph_dml_reference_at(
	const sqlparser_query_graph_view_t *graph,
	size_t reference_index,
	sqlparser_graph_dml_reference_t *out_reference,
	sqlparser_error_t *out_error)
{
	sqlparser_query_graph_cache_t *cache;
	sqlparser_statement_graph_t *statement;

	sqlparser_error_clear(out_error);
	if (out_reference == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "out_reference must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	memset(out_reference, 0, sizeof(*out_reference));
	statement = sqlparser_query_graph_statement(graph, &cache);
	if (statement == NULL || cache->dml_results == NULL ||
	    reference_index >= statement->dml_reference_count) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "DML reference index is out of range");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_reference = cache->dml_results->references[
		statement->dml_reference_offset + reference_index];
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_query_graph_dml_branch_at(
	const sqlparser_query_graph_view_t *graph,
	size_t branch_index,
	sqlparser_graph_dml_branch_t *out_branch,
	sqlparser_error_t *out_error)
{
	sqlparser_query_graph_cache_t *cache;
	sqlparser_statement_graph_t *statement;

	sqlparser_error_clear(out_error);
	if (out_branch == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "out_branch must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	memset(out_branch, 0, sizeof(*out_branch));
	statement = sqlparser_query_graph_statement(graph, &cache);
	if (statement == NULL || branch_index >= statement->dml_branch_count) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "dml branch index is out of range");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_branch = cache->dml_branches[statement->dml_branch_offset + branch_index];
	return SQLPARSER_STATUS_OK;
}

static const sqlparser_graph_merge_branch_detail_t *
sqlparser_query_graph_merge_branch_detail_entry(
	const sqlparser_query_graph_view_t *graph,
	size_t branch_index)
{
	sqlparser_query_graph_cache_t *cache;
	sqlparser_statement_graph_t *statement;

	statement = sqlparser_query_graph_statement(graph, &cache);
	if (statement == NULL ||
	    branch_index >= statement->dml_branch_count) {
		return NULL;
	}
	return &cache->merge_branch_details[
		statement->dml_branch_offset + branch_index];
}

sqlparser_status_t sqlparser_query_graph_merge_branch_detail(
	const sqlparser_query_graph_view_t *graph,
	size_t branch_index,
	sqlparser_graph_merge_action_kind_t *out_action_kind,
	sqlparser_graph_merge_match_kind_t *out_match_kind,
	sqlparser_index_span_t *out_assignments,
	sqlparser_error_t *out_error)
{
	const sqlparser_graph_merge_branch_detail_t *detail;

	sqlparser_error_clear(out_error);
	if (out_action_kind != NULL) {
		*out_action_kind = SQLPARSER_GRAPH_MERGE_ACTION_UNKNOWN;
	}
	if (out_match_kind != NULL) {
		*out_match_kind = SQLPARSER_GRAPH_MERGE_MATCH_UNKNOWN;
	}
	if (out_assignments != NULL) {
		memset(out_assignments, 0, sizeof(*out_assignments));
	}
	if (out_action_kind == NULL ||
	    out_match_kind == NULL ||
	    out_assignments == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"MERGE branch detail outputs must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	detail = sqlparser_query_graph_merge_branch_detail_entry(
		graph,
		branch_index);
	if (detail == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"dml branch index is out of range");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	if (detail->action_kind ==
		    SQLPARSER_GRAPH_MERGE_ACTION_UNKNOWN ||
	    detail->match_kind ==
		    SQLPARSER_GRAPH_MERGE_MATCH_UNKNOWN) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_UNSUPPORTED,
			"dml branch is not a MERGE WHEN branch");
		return SQLPARSER_STATUS_UNSUPPORTED;
	}
	*out_action_kind = detail->action_kind;
	*out_match_kind = detail->match_kind;
	*out_assignments = detail->assignments;
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_query_graph_dml_column_at(
	const sqlparser_query_graph_view_t *graph,
	size_t column_index,
	sqlparser_graph_dml_column_t *out_column,
	sqlparser_error_t *out_error)
{
	sqlparser_query_graph_cache_t *cache;
	sqlparser_statement_graph_t *statement;

	sqlparser_error_clear(out_error);
	if (out_column == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "out_column must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	memset(out_column, 0, sizeof(*out_column));
	statement = sqlparser_query_graph_statement(graph, &cache);
	if (statement == NULL || column_index >= statement->dml_column_count) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "dml column index is out of range");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_column = cache->dml_columns[statement->dml_column_offset + column_index];
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_query_graph_dml_cell_at(
	const sqlparser_query_graph_view_t *graph,
	size_t cell_index,
	sqlparser_graph_dml_cell_t *out_cell,
	sqlparser_error_t *out_error)
{
	sqlparser_query_graph_cache_t *cache;
	sqlparser_graph_dml_cell_t cell;
	sqlparser_statement_graph_t *statement;
	size_t global_index;

	sqlparser_error_clear(out_error);
	if (out_cell == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "out_cell must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	memset(out_cell, 0, sizeof(*out_cell));
	statement = sqlparser_query_graph_statement(graph, &cache);
	if (statement == NULL || cell_index >= statement->dml_cell_count) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "dml cell index is out of range");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	if (statement->dml_cell_offset > SIZE_MAX - cell_index) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "dml cell offset is invalid");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}
	global_index = statement->dml_cell_offset + cell_index;
	if (global_index >= cache->dml_cell_count ||
	    sqlparser_graph_dml_cell_cache_copy_public(
		    cache,
		    &cache->dml_cells[global_index],
		    graph->statement_index,
		    cell_index,
		    &cell,
		    out_error) != 0) {
		if (out_error != NULL && out_error->code == SQLPARSER_STATUS_OK) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "dml cell cache is invalid");
		}
		return out_error != NULL ? out_error->code : SQLPARSER_STATUS_INTERNAL_ERROR;
	}
	*out_cell = cell;
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_query_graph_dml_assignment_at(
	const sqlparser_query_graph_view_t *graph,
	size_t assignment_index,
	sqlparser_graph_dml_assignment_t *out_assignment,
	sqlparser_error_t *out_error)
{
	sqlparser_query_graph_cache_t *cache;
	sqlparser_statement_graph_t *statement;

	sqlparser_error_clear(out_error);
	if (out_assignment == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "out_assignment must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	memset(out_assignment, 0, sizeof(*out_assignment));
	statement = sqlparser_query_graph_statement(graph, &cache);
	if (statement == NULL || assignment_index >= statement->dml_assignment_count) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "dml assignment index is out of range");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_assignment = cache->dml_assignments[statement->dml_assignment_offset + assignment_index];
	return SQLPARSER_STATUS_OK;
}

static json_t *sqlparser_graph_span_json(
	const sqlparser_query_graph_view_t *graph,
	sqlparser_index_span_t span,
	sqlparser_error_t *out_error)
{
	json_t *array;
	size_t index;

	array = json_array();
	if (array == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return NULL;
	}
	for (index = 0U; index < span.count; index++) {
		size_t value;

		value = 0U;
		if (sqlparser_query_graph_span_index_at(graph, span, index, &value, out_error) != SQLPARSER_STATUS_OK ||
		    json_array_append_new(array, json_integer((json_int_t)value)) != 0) {
			json_decref(array);
			if (out_error != NULL && out_error->code == SQLPARSER_STATUS_OK) {
				sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			}
			return NULL;
		}
	}
	return array;
}

static json_t *sqlparser_graph_block_json(
	const sqlparser_query_graph_view_t *graph,
	const sqlparser_graph_block_t *block,
	sqlparser_error_t *out_error)
{
	json_t *object;
	json_t *relations;
	json_t *targets;
	json_t *predicates;

	object = json_object();
	relations = sqlparser_graph_span_json(graph, block->relations, out_error);
	targets = sqlparser_graph_span_json(graph, block->targets, out_error);
	predicates = sqlparser_graph_span_json(graph, block->predicates, out_error);
	if (object == NULL || relations == NULL || targets == NULL || predicates == NULL ||
	    json_object_set_new(object, "kind", json_string(sqlparser_graph_block_kind_name(block->kind))) != 0 ||
	    sqlparser_json_set_nonempty_array(object, "relations", &relations) != 0 ||
	    sqlparser_json_set_nonempty_array(object, "targets", &targets) != 0 ||
	    sqlparser_json_set_nonempty_array(object, "predicates", &predicates) != 0) {
		json_decref(object);
		json_decref(relations);
		json_decref(targets);
		json_decref(predicates);
		if (out_error != NULL && out_error->code == SQLPARSER_STATUS_OK) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		}
		return NULL;
	}
	return object;
}

static json_t *sqlparser_graph_relation_json(
	const sqlparser_graph_relation_t *relation,
	sqlparser_error_t *out_error)
{
	json_t *object;

	object = json_object();
	if (object == NULL ||
	    json_object_set_new(object, "block", json_integer((json_int_t)relation->block_index)) != 0 ||
	    json_object_set_new(object, "kind", json_string(sqlparser_graph_relation_kind_name(relation->kind))) != 0 ||
	    sqlparser_json_set_optional_string(object, "database", relation->database_name) != 0 ||
	    sqlparser_json_set_optional_string(object, "schema", relation->schema_name) != 0 ||
	    sqlparser_json_set_optional_string(object, "table", relation->object_name) != 0 ||
	    (relation->quoted_identifier &&
	     json_object_set_new(object, "quoted_identifier", json_boolean(1)) != 0) ||
	    sqlparser_json_set_optional_string(object, "alias", relation->alias_name) != 0 ||
	    sqlparser_json_set_optional_string(object, "link", relation->link_name) != 0 ||
	    sqlparser_json_set_optional_size(object, "source_block", relation->has_source_block, relation->source_block_index) != 0 ||
	    sqlparser_json_set_optional_selector(object, "selector", relation->has_selector ? &relation->selector : NULL, out_error) != 0) {
		json_decref(object);
		if (out_error != NULL && out_error->code == SQLPARSER_STATUS_OK) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		}
		return NULL;
	}
	return object;
}

static json_t *sqlparser_graph_target_json(
	const sqlparser_query_graph_view_t *graph,
	const sqlparser_graph_target_t *target,
	sqlparser_error_t *out_error)
{
	json_t *object;
	json_t *star_relations;

	object = json_object();
	star_relations = sqlparser_graph_span_json(graph, target->star_relations, out_error);
	if (object == NULL || star_relations == NULL ||
	    json_object_set_new(object, "block", json_integer((json_int_t)target->block_index)) != 0 ||
	    json_object_set_new(object, "ordinal", json_integer((json_int_t)target->ordinal)) != 0 ||
	    json_object_set_new(object, "kind", json_string(sqlparser_graph_target_kind_name(target->kind))) != 0 ||
	    sqlparser_json_set_optional_string(object, "name", target->output_name) != 0 ||
	    sqlparser_json_set_optional_size(object, "field", target->has_field, target->field_index) != 0 ||
	    sqlparser_json_set_optional_size(object, "value", target->has_value, target->value_index) != 0 ||
	    sqlparser_json_set_nonempty_array(object, "star_relations", &star_relations) != 0 ||
	    sqlparser_json_set_optional_size(object, "source_block", target->has_source_block, target->source_block_index) != 0 ||
	    sqlparser_json_set_optional_selector(object, "selector", target->has_selector ? &target->selector : NULL, out_error) != 0 ||
	    sqlparser_json_set_optional_size(object, "sink_value", target->has_sink_value, target->sink_value_index) != 0 ||
	    sqlparser_json_set_optional_selector(object, "target_list_selector", target->has_target_list_selector ? &target->target_list_selector : NULL, out_error) != 0) {
		json_decref(object);
		json_decref(star_relations);
		if (out_error != NULL && out_error->code == SQLPARSER_STATUS_OK) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		}
		return NULL;
	}
	return object;
}

static json_t *sqlparser_graph_target_path_json(
	const sqlparser_graph_field_t *field,
	sqlparser_error_t *out_error)
{
	json_t *array;
	size_t index;

	array = json_array();
	if (array == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return NULL;
	}
	for (index = 0U; index < field->target_path_count; index++) {
		const sqlparser_target_path_entry_t *path;
		json_t *entry;

		path = &field->target_path[index];
		entry = json_object();
		if (entry == NULL ||
			    sqlparser_json_set_optional_string(entry, "kind", path->kind) != 0 ||
			    sqlparser_json_set_optional_string(entry, "name", path->has_name ? path->name : NULL) != 0 ||
		    json_object_set_new(entry, "arg_index", json_integer((json_int_t)path->arg_index)) != 0 ||
		    sqlparser_json_array_append_owned(array, &entry) != 0) {
		json_decref(entry);
		json_decref(array);
		if (out_error != NULL && out_error->code == SQLPARSER_STATUS_OK) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			}
			return NULL;
		}
	}
	return array;
}

static json_t *sqlparser_graph_field_json(
	const sqlparser_query_graph_view_t *graph,
	const sqlparser_graph_field_t *field,
	sqlparser_error_t *out_error)
{
	json_t *object;
	json_t *candidates;
	json_t *target_path;

	object = json_object();
	candidates = sqlparser_graph_span_json(graph, field->candidate_relations, out_error);
	target_path = sqlparser_graph_target_path_json(field, out_error);
	if (object == NULL || candidates == NULL || target_path == NULL ||
	    json_object_set_new(object, "block", json_integer((json_int_t)field->block_index)) != 0 ||
	    json_object_set_new(object, "clause", json_string(sqlparser_clause_kind_name(field->clause))) != 0 ||
	    sqlparser_json_set_optional_size(object, "relation", field->has_relation, field->relation_index) != 0 ||
	    sqlparser_json_set_nonempty_array(object, "candidate_relations", &candidates) != 0 ||
	    sqlparser_json_set_optional_string(object, "column", field->column_name) != 0 ||
	    (field->quoted_identifier &&
	     json_object_set_new(object, "quoted_identifier", json_boolean(1)) != 0) ||
	    (field->pseudo &&
	     json_object_set_new(object, "pseudo", json_boolean(1)) != 0) ||
	    (field->prior &&
	     json_object_set_new(object, "prior", json_boolean(1)) != 0) ||
	    sqlparser_json_set_optional_size(object, "target", field->has_target, field->target_index) != 0 ||
	    sqlparser_json_set_optional_selector(object, "selector", field->has_selector ? &field->selector : NULL, out_error) != 0 ||
	    sqlparser_json_set_nonempty_array(object, "target_path", &target_path) != 0) {
		json_decref(object);
		json_decref(candidates);
		json_decref(target_path);
		if (out_error != NULL && out_error->code == SQLPARSER_STATUS_OK) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		}
		return NULL;
	}
	return object;
}

static json_t *sqlparser_graph_literal_json(
	const sqlparser_literal_view_t *literal,
	int include_literal)
{
	json_t *object;

	if (!include_literal || literal == NULL) {
		return json_null();
	}
	object = json_object();
	if (object == NULL ||
	    json_object_set_new(object, "kind", json_string(sqlparser_literal_kind_name(literal->kind))) != 0) {
		json_decref(object);
		return NULL;
	}
	switch (literal->kind) {
		case SQLPARSER_LITERAL_KIND_STRING:
			if (json_object_set_new(object, "string_value", json_string(literal->string_value != NULL ? literal->string_value : "")) != 0) {
				json_decref(object);
				return NULL;
			}
			if (literal->quoted_identifier &&
			    json_object_set_new(object, "quoted_identifier", json_boolean(1)) != 0) {
				json_decref(object);
				return NULL;
			}
			break;
		case SQLPARSER_LITERAL_KIND_INTEGER:
			if (json_object_set_new(object, "integer_value", json_integer((json_int_t)literal->integer_value)) != 0) {
				json_decref(object);
				return NULL;
			}
			break;
		case SQLPARSER_LITERAL_KIND_FLOAT:
			if (sqlparser_json_set_optional_string(object, "float_value", literal->float_value) != 0) {
				json_decref(object);
				return NULL;
			}
			break;
		case SQLPARSER_LITERAL_KIND_BOOLEAN:
			if (json_object_set_new(object, "boolean_value", json_boolean(literal->boolean_value != 0)) != 0) {
				json_decref(object);
				return NULL;
			}
			break;
		case SQLPARSER_LITERAL_KIND_NULL:
		case SQLPARSER_LITERAL_KIND_UNKNOWN:
		default:
			break;
	}
	return object;
}

static const char *sqlparser_graph_session_action_name(
	sqlparser_graph_session_action_t action)
{
	switch (action) {
		case SQLPARSER_GRAPH_SESSION_ACTION_SET:
			return "set";
		case SQLPARSER_GRAPH_SESSION_ACTION_RESET:
			return "reset";
		case SQLPARSER_GRAPH_SESSION_ACTION_SWITCH:
			return "switch";
		case SQLPARSER_GRAPH_SESSION_ACTION_DISCARD:
			return "discard";
		case SQLPARSER_GRAPH_SESSION_ACTION_ENABLE:
			return "enable";
		case SQLPARSER_GRAPH_SESSION_ACTION_DISABLE:
			return "disable";
		case SQLPARSER_GRAPH_SESSION_ACTION_FORCE:
			return "force";
		case SQLPARSER_GRAPH_SESSION_ACTION_ADVISE:
			return "advise";
		case SQLPARSER_GRAPH_SESSION_ACTION_CLOSE:
			return "close";
		case SQLPARSER_GRAPH_SESSION_ACTION_SYNC:
			return "sync";
		case SQLPARSER_GRAPH_SESSION_ACTION_ASSUME:
			return "assume";
		case SQLPARSER_GRAPH_SESSION_ACTION_REVERT:
			return "revert";
		case SQLPARSER_GRAPH_SESSION_ACTION_UNKNOWN:
		default:
			return "unknown";
	}
}

static const char *sqlparser_graph_session_scope_name(
	sqlparser_graph_session_scope_t scope)
{
	switch (scope) {
		case SQLPARSER_GRAPH_SESSION_SCOPE_SESSION:
			return "session";
		case SQLPARSER_GRAPH_SESSION_SCOPE_LOCAL:
			return "local";
		case SQLPARSER_GRAPH_SESSION_SCOPE_TRANSACTION:
			return "transaction";
		case SQLPARSER_GRAPH_SESSION_SCOPE_UNKNOWN:
		default:
			return "unknown";
	}
}

static const char *sqlparser_graph_session_target_name(
	sqlparser_graph_session_target_kind_t kind)
{
	switch (kind) {
		case SQLPARSER_GRAPH_SESSION_TARGET_PARAMETER:
			return "parameter";
		case SQLPARSER_GRAPH_SESSION_TARGET_VARIABLE:
			return "variable";
		case SQLPARSER_GRAPH_SESSION_TARGET_DATABASE:
			return "database";
		case SQLPARSER_GRAPH_SESSION_TARGET_SCHEMA:
			return "schema";
		case SQLPARSER_GRAPH_SESSION_TARGET_CONTAINER:
			return "container";
		case SQLPARSER_GRAPH_SESSION_TARGET_ROLE:
			return "role";
		case SQLPARSER_GRAPH_SESSION_TARGET_AUTHORIZATION:
			return "authorization";
		case SQLPARSER_GRAPH_SESSION_TARGET_LOGIN:
			return "login";
		case SQLPARSER_GRAPH_SESSION_TARGET_USER:
			return "user";
		case SQLPARSER_GRAPH_SESSION_TARGET_TRANSACTION:
			return "transaction";
		case SQLPARSER_GRAPH_SESSION_TARGET_SESSION_CONTEXT:
			return "session_context";
		case SQLPARSER_GRAPH_SESSION_TARGET_DATABASE_LINK:
			return "database_link";
		case SQLPARSER_GRAPH_SESSION_TARGET_OBJECT:
			return "object";
		case SQLPARSER_GRAPH_SESSION_TARGET_CONSTRAINT:
			return "constraint";
		case SQLPARSER_GRAPH_SESSION_TARGET_ALL:
			return "all";
		case SQLPARSER_GRAPH_SESSION_TARGET_UNKNOWN:
		default:
			return "unknown";
	}
}

static const char *sqlparser_graph_session_value_name(
	sqlparser_graph_session_value_kind_t kind)
{
	switch (kind) {
		case SQLPARSER_GRAPH_SESSION_VALUE_IDENTIFIER:
			return "identifier";
		case SQLPARSER_GRAPH_SESSION_VALUE_KEYWORD:
			return "keyword";
		case SQLPARSER_GRAPH_SESSION_VALUE_LITERAL:
			return "literal";
		case SQLPARSER_GRAPH_SESSION_VALUE_BIND:
			return "bind";
		case SQLPARSER_GRAPH_SESSION_VALUE_EXPRESSION:
			return "expression";
		case SQLPARSER_GRAPH_SESSION_VALUE_UNKNOWN:
		default:
			return "unknown";
	}
}

static json_t *sqlparser_graph_session_value_json(
	const sqlparser_graph_session_value_t *value,
	sqlparser_error_t *out_error)
{
	json_t *object;
	json_t *literal;

	object = json_object();
	literal = sqlparser_graph_literal_json(
		&value->literal,
		value->kind == SQLPARSER_GRAPH_SESSION_VALUE_LITERAL);
	if (object == NULL || literal == NULL ||
	    sqlparser_json_set_optional_string(object, "name", value->name) != 0 ||
	    json_object_set_new(
		    object,
		    "kind",
		    json_string(sqlparser_graph_session_value_name(value->kind))) != 0 ||
	    sqlparser_json_set_optional_string(object, "text", value->text) != 0) {
		json_decref(object);
		json_decref(literal);
		goto fail;
	}
	if (value->kind == SQLPARSER_GRAPH_SESSION_VALUE_IDENTIFIER &&
	    value->literal.quoted_identifier &&
	    json_object_set_new(
		    object,
		    "quoted_identifier",
		    json_boolean(1)) != 0) {
		json_decref(object);
		json_decref(literal);
		goto fail;
	}
	if (value->kind == SQLPARSER_GRAPH_SESSION_VALUE_BIND &&
	    (sqlparser_json_set_optional_string(object, "bind_key", value->bind_key) != 0 ||
	     json_object_set_new(object, "bind_kind", json_integer(value->bind_kind)) != 0 ||
	     sqlparser_json_set_optional_string(object, "bind_sql", value->bind_sql) != 0 ||
	     sqlparser_json_set_optional_size(
		     object,
		     "bind_position",
		     value->has_bind_position,
		     value->bind_position) != 0)) {
		json_decref(object);
		json_decref(literal);
		goto fail;
	}
	if (json_is_null(literal)) {
		json_decref(literal);
	} else if (sqlparser_json_object_set_owned(object, "literal", &literal) != 0) {
		json_decref(object);
		json_decref(literal);
		goto fail;
	}
	return object;

fail:
	if (out_error != NULL && out_error->code == SQLPARSER_STATUS_OK) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
	}
	return NULL;
}

static json_t *sqlparser_graph_session_item_json(
	const sqlparser_query_graph_view_t *graph,
	const sqlparser_graph_session_item_t *item,
	sqlparser_error_t *out_error)
{
	json_t *object;
	json_t *values;
	size_t index;

	object = json_object();
	values = json_array();
	if (object == NULL || values == NULL ||
	    json_object_set_new(
		    object,
		    "scope",
		    json_string(sqlparser_graph_session_scope_name(item->scope))) != 0 ||
	    json_object_set_new(
		    object,
		    "target_kind",
		    json_string(sqlparser_graph_session_target_name(item->target_kind))) != 0 ||
	    sqlparser_json_set_optional_string(object, "name", item->name) != 0) {
		goto fail;
	}
	for (index = 0U; index < item->value_count; index++) {
		sqlparser_graph_session_value_t value;
		json_t *entry;
		size_t value_index;

		if (item->value_offset > (size_t)-1 - index) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "session value index is invalid");
			goto fail;
		}
		value_index = item->value_offset + index;
		if (sqlparser_query_graph_session_value_at(
			    graph,
			    value_index,
			    &value,
			    out_error) != SQLPARSER_STATUS_OK) {
			goto fail;
		}
		entry = sqlparser_graph_session_value_json(&value, out_error);
		if (entry == NULL || sqlparser_json_array_append_owned(values, &entry) != 0) {
			json_decref(entry);
			goto fail;
		}
	}
	if (sqlparser_json_set_nonempty_array(object, "values", &values) != 0) {
		goto fail;
	}
	return object;

fail:
	json_decref(object);
	json_decref(values);
	if (out_error != NULL && out_error->code == SQLPARSER_STATUS_OK) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
	}
	return NULL;
}

static json_t *sqlparser_graph_session_json(
	const sqlparser_query_graph_view_t *graph,
	sqlparser_error_t *out_error)
{
	sqlparser_graph_session_t session;
	json_t *object;
	json_t *items;
	size_t index;

	memset(&session, 0, sizeof(session));
	if (sqlparser_query_graph_session(graph, &session, out_error) != SQLPARSER_STATUS_OK) {
		return NULL;
	}
	if (session.action == SQLPARSER_GRAPH_SESSION_ACTION_UNKNOWN) {
		return json_null();
	}
	object = json_object();
	items = json_array();
	if (object == NULL || items == NULL ||
	    json_object_set_new(
		    object,
		    "action",
		    json_string(sqlparser_graph_session_action_name(session.action))) != 0) {
		goto fail;
	}
	for (index = 0U; index < session.item_count; index++) {
		sqlparser_graph_session_item_t item;
		json_t *entry;

		if (sqlparser_query_graph_session_item_at(
			    graph,
			    index,
			    &item,
			    out_error) != SQLPARSER_STATUS_OK) {
			goto fail;
		}
		entry = sqlparser_graph_session_item_json(graph, &item, out_error);
		if (entry == NULL || sqlparser_json_array_append_owned(items, &entry) != 0) {
			json_decref(entry);
			goto fail;
		}
	}
	if (sqlparser_json_set_nonempty_array(object, "items", &items) != 0) {
		goto fail;
	}
	return object;

fail:
	json_decref(object);
	json_decref(items);
	if (out_error != NULL && out_error->code == SQLPARSER_STATUS_OK) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
	}
	return NULL;
}

static int sqlparser_graph_like_escape_literal_json(json_t *object, const sqlparser_literal_view_t *literal)
{
	if (object == NULL || literal == NULL) {
		return -1;
	}
	if (json_object_set_new(object, "literal_kind", json_string(sqlparser_literal_kind_name(literal->kind))) != 0) {
		return -1;
	}
	switch (literal->kind) {
		case SQLPARSER_LITERAL_KIND_STRING:
			return json_object_set_new(
				object,
				"literal_value",
				json_string(literal->string_value != NULL ? literal->string_value : ""));
		case SQLPARSER_LITERAL_KIND_INTEGER:
			return json_object_set_new(object, "integer_value", json_integer((json_int_t)literal->integer_value));
		case SQLPARSER_LITERAL_KIND_FLOAT:
			return sqlparser_json_set_optional_string(object, "literal_value", literal->float_value);
		case SQLPARSER_LITERAL_KIND_BOOLEAN:
			return json_object_set_new(object, "boolean_value", json_boolean(literal->boolean_value != 0));
		case SQLPARSER_LITERAL_KIND_NULL:
		case SQLPARSER_LITERAL_KIND_UNKNOWN:
		default:
			return 0;
	}
}

static json_t *sqlparser_graph_like_escape_json(const sqlparser_graph_like_escape_t *escape)
{
	json_t *object;

	if (escape == NULL || escape->kind == SQLPARSER_GRAPH_LIKE_ESCAPE_NONE) {
		return json_null();
	}
	object = json_object();
	if (object == NULL ||
	    json_object_set_new(object, "kind", json_string(sqlparser_graph_like_escape_kind_name(escape->kind))) != 0) {
		json_decref(object);
		return NULL;
	}
	switch (escape->kind) {
		case SQLPARSER_GRAPH_LIKE_ESCAPE_LITERAL:
			if (sqlparser_graph_like_escape_literal_json(object, &escape->literal) != 0) {
				json_decref(object);
				return NULL;
			}
			break;
		case SQLPARSER_GRAPH_LIKE_ESCAPE_BIND:
			if (sqlparser_json_set_optional_string(object, "bind_key", escape->has_bind ? escape->bind : NULL) != 0 ||
			    json_object_set_new(object, "bind_kind", json_integer(escape->bind_kind)) != 0 ||
			    sqlparser_json_set_optional_string(object, "bind_sql", escape->has_bind_sql ? escape->bind_sql : NULL) != 0 ||
			    sqlparser_json_set_optional_size(object, "bind_position", escape->has_bind_position, escape->bind_position) != 0) {
				json_decref(object);
				return NULL;
			}
			break;
		case SQLPARSER_GRAPH_LIKE_ESCAPE_EXPRESSION:
		case SQLPARSER_GRAPH_LIKE_ESCAPE_NONE:
		default:
			break;
	}
	return object;
}

static json_t *sqlparser_graph_value_json(
	const sqlparser_query_graph_view_t *graph,
	const sqlparser_graph_value_t *value,
	sqlparser_error_t *out_error)
{
	json_t *object;
	json_t *literal;
	json_t *like_escape;
	const char *field_match_kind_name;
	const char *operator_kind_name;

	(void)graph;
	field_match_kind_name = value->has_field &&
			value->field_match_kind != SQLPARSER_GRAPH_FIELD_MATCH_UNKNOWN ?
		sqlparser_graph_field_match_kind_name(value->field_match_kind) :
		NULL;
	operator_kind_name = value->operator_name != NULL ?
		sqlparser_graph_operator_kind_name(value->operator_kind) :
		NULL;
	object = json_object();
	literal = sqlparser_graph_literal_json(
		&value->literal,
		value->kind == SQLPARSER_GRAPH_VALUE_LITERAL);
	like_escape = sqlparser_graph_like_escape_json(&value->like_escape);
	if (object == NULL || literal == NULL || like_escape == NULL ||
	    json_object_set_new(object, "block", json_integer((json_int_t)value->block_index)) != 0 ||
	    json_object_set_new(object, "clause", json_string(sqlparser_clause_kind_name(value->clause))) != 0 ||
	    sqlparser_json_set_optional_string(object, "operator", value->operator_name) != 0 ||
	    sqlparser_json_set_optional_string(object, "operator_kind", operator_kind_name) != 0 ||
	    sqlparser_json_set_optional_size(object, "field", value->has_field, value->field_index) != 0 ||
	    sqlparser_json_set_optional_size(object, "source_field", value->has_source_field, value->source_field_index) != 0 ||
	    sqlparser_json_set_optional_string(object, "field_match_kind", field_match_kind_name) != 0 ||
	    json_object_set_new(object, "kind", json_string(sqlparser_graph_value_kind_name(value->kind))) != 0 ||
	    sqlparser_json_set_optional_string(object, "bind_key", value->has_bind ? value->bind : NULL) != 0 ||
	    json_object_set_new(object, "bind_kind", json_integer(value->bind_kind)) != 0 ||
	    sqlparser_json_set_optional_string(object, "bind_sql", value->has_bind_sql ? value->bind_sql : NULL) != 0 ||
	    sqlparser_json_set_optional_size(object, "bind_position", value->has_bind_position, value->bind_position) != 0 ||
	    sqlparser_json_set_optional_selector(object, "selector", value->has_selector ? &value->selector : NULL, out_error) != 0) {
		json_decref(object);
		json_decref(literal);
		json_decref(like_escape);
		if (out_error != NULL && out_error->code == SQLPARSER_STATUS_OK) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		}
		return NULL;
	}
	if (json_is_null(literal)) {
		json_decref(literal);
		literal = NULL;
	} else if (sqlparser_json_object_set_owned(object, "literal", &literal) != 0) {
		json_decref(object);
		json_decref(literal);
		if (out_error != NULL && out_error->code == SQLPARSER_STATUS_OK) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		}
		return NULL;
	}
	if (json_is_null(like_escape)) {
		json_decref(like_escape);
		like_escape = NULL;
	} else if (sqlparser_json_object_set_owned(object, "like_escape", &like_escape) != 0) {
		json_decref(object);
		json_decref(like_escape);
		if (out_error != NULL && out_error->code == SQLPARSER_STATUS_OK) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		}
		return NULL;
	}
	return object;
}

static json_t *sqlparser_graph_set_json(
	const sqlparser_handle_t *handle,
	const sqlparser_query_graph_view_t *graph,
	const sqlparser_graph_set_t *set_item,
	sqlparser_error_t *out_error)
{
	json_t *object;
	json_t *branches;
	const char *kind_name;

	object = json_object();
	branches = sqlparser_graph_span_json(graph, set_item->branch_blocks, out_error);
	kind_name = sqlparser_graph_set_kind_name(set_item->kind);
	if (set_item->kind == SQLPARSER_GRAPH_SET_EXCEPT &&
	    handle != NULL &&
	    sqlparser_dialect_is_oracle_or_dameng_compatible(handle->dialect)) {
		kind_name = "minus";
	}
	if (object == NULL || branches == NULL ||
	    json_object_set_new(object, "kind", json_string(kind_name)) != 0 ||
	    json_object_set_new(object, "result_block", json_integer((json_int_t)set_item->result_block_index)) != 0 ||
	    sqlparser_json_object_set_owned(object, "branches", &branches) != 0) {
		json_decref(object);
		json_decref(branches);
		if (out_error != NULL && out_error->code == SQLPARSER_STATUS_OK) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		}
		return NULL;
	}
	return object;
}

static json_t *sqlparser_graph_predicate_json(
	const sqlparser_query_graph_view_t *graph,
	const sqlparser_graph_predicate_t *predicate,
	sqlparser_error_t *out_error)
{
	json_t *object;
	json_t *children;
	const char *operator_kind_name;

	object = json_object();
	children = sqlparser_graph_span_json(graph, predicate->children, out_error);
	operator_kind_name = predicate->operator_name != NULL ?
		sqlparser_graph_operator_kind_name(predicate->operator_kind) :
		NULL;
	if (object == NULL || children == NULL ||
	    json_object_set_new(object, "block", json_integer((json_int_t)predicate->block_index)) != 0 ||
	    json_object_set_new(object, "clause", json_string(sqlparser_clause_kind_name(predicate->clause))) != 0 ||
	    json_object_set_new(object, "kind", json_string(sqlparser_graph_predicate_kind_name(predicate->kind))) != 0 ||
	    json_object_set_new(object, "bool_operator", json_string(sqlparser_graph_predicate_bool_name(predicate->bool_operator))) != 0 ||
	    (predicate->nocycle &&
	     json_object_set_new(object, "nocycle", json_boolean(1)) != 0) ||
	    sqlparser_json_set_optional_string(object, "operator", predicate->operator_name) != 0 ||
	    sqlparser_json_set_optional_string(object, "operator_kind", operator_kind_name) != 0 ||
	    sqlparser_json_set_optional_size(object, "left_field", predicate->has_left_field, predicate->left_field_index) != 0 ||
	    sqlparser_json_set_optional_size(object, "right_field", predicate->has_right_field, predicate->right_field_index) != 0 ||
	    sqlparser_json_set_optional_size(object, "value", predicate->has_value, predicate->value_index) != 0 ||
	    sqlparser_json_set_nonempty_array(object, "children", &children) != 0) {
		json_decref(object);
		json_decref(children);
		if (out_error != NULL && out_error->code == SQLPARSER_STATUS_OK) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		}
		return NULL;
	}
	return object;
}

static json_t *sqlparser_graph_dml_column_json(
	const sqlparser_graph_dml_column_t *column,
	sqlparser_error_t *out_error)
{
	json_t *object;

	object = json_object();
	if (object == NULL ||
	    json_object_set_new(object, "ordinal", json_integer((json_int_t)column->ordinal)) != 0 ||
	    sqlparser_json_set_optional_string(object, "column", column->column_name) != 0 ||
	    sqlparser_json_set_optional_selector(object, "selector", column->has_selector ? &column->selector : NULL, out_error) != 0) {
		json_decref(object);
		if (out_error != NULL && out_error->code == SQLPARSER_STATUS_OK) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		}
		return NULL;
	}
	return object;
}

static const char *sqlparser_graph_dml_cell_expression_sql(
	const sqlparser_query_graph_view_t *graph,
	const sqlparser_graph_dml_cell_t *cell,
	sqlparser_error_t *out_error)
{
	sqlparser_query_graph_cache_t *cache;
	const sqlparser_graph_dml_cell_cache_t *cached_cell;
	sqlparser_statement_graph_t *statement;
	size_t global_index;

	cache = NULL;
	statement = sqlparser_query_graph_statement(graph, &cache);
	if (statement == NULL || cache == NULL ||
	    cell->index >= statement->dml_cell_count ||
	    statement->dml_cell_offset > (size_t)-1 - cell->index) {
		return NULL;
	}
	global_index = statement->dml_cell_offset + cell->index;
	if (global_index >= cache->dml_cell_count) {
		return NULL;
	}
	cached_cell = &cache->dml_cells[global_index];
	if (cached_cell->kind != SQLPARSER_GRAPH_VALUE_EXPRESSION ||
	    (cached_cell->flags &
	     SQLPARSER_GRAPH_DML_CELL_HAS_EXPRESSION_SQL) == 0U) {
		return NULL;
	}
	return sqlparser_graph_value_text_at(
		cache,
		cached_cell->payload.expression_sql_offset,
		out_error);
}

static json_t *sqlparser_graph_dml_cell_json(
	const sqlparser_query_graph_view_t *graph,
	const sqlparser_graph_dml_cell_t *cell,
	sqlparser_error_t *out_error)
{
	const char *expression_sql;
	json_t *object;
	json_t *literal;

	expression_sql = NULL;
	if (cell->kind == SQLPARSER_GRAPH_VALUE_EXPRESSION) {
		expression_sql =
			sqlparser_graph_dml_cell_expression_sql(
				graph,
				cell,
				out_error);
		if (expression_sql == NULL || expression_sql[0] == '\0') {
			if (out_error == NULL ||
			    out_error->code == SQLPARSER_STATUS_OK) {
				sqlparser_error_set_message(
					out_error,
					SQLPARSER_STATUS_INTERNAL_ERROR,
					"expression cell SQL is missing");
			}
			return NULL;
		}
	}
	object = json_object();
	literal = sqlparser_graph_literal_json(
		&cell->literal,
		cell->kind == SQLPARSER_GRAPH_VALUE_LITERAL);
	if (object == NULL || literal == NULL ||
	    json_object_set_new(object, "row", json_integer((json_int_t)cell->row_index)) != 0 ||
	    json_object_set_new(object, "column", json_integer((json_int_t)cell->column_ordinal)) != 0 ||
	    json_object_set_new(object, "kind", json_string(sqlparser_graph_value_kind_name(cell->kind))) != 0 ||
	    sqlparser_json_set_optional_size(object, "source_target", cell->has_source_target, cell->source_target_index) != 0 ||
	    sqlparser_json_set_optional_size(object, "source_field", cell->has_source_field, cell->source_field_index) != 0 ||
	    sqlparser_json_set_optional_string(object, "bind_key", cell->has_bind ? cell->bind : NULL) != 0 ||
	    json_object_set_new(object, "bind_kind", json_integer(cell->bind_kind)) != 0 ||
	    sqlparser_json_set_optional_string(object, "bind_sql", cell->has_bind_sql ? cell->bind_sql : NULL) != 0 ||
	    sqlparser_json_set_optional_size(object, "bind_position", cell->has_bind_position, cell->bind_position) != 0 ||
	    sqlparser_json_set_optional_selector(object, "selector", cell->has_selector ? &cell->selector : NULL, out_error) != 0 ||
	    sqlparser_json_set_optional_string(object, "expression_sql", expression_sql) != 0) {
		json_decref(object);
		json_decref(literal);
		if (out_error != NULL && out_error->code == SQLPARSER_STATUS_OK) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		}
		return NULL;
	}
	if (json_is_null(literal)) {
		json_decref(literal);
		literal = NULL;
	} else if (sqlparser_json_object_set_owned(object, "literal", &literal) != 0) {
		json_decref(object);
		json_decref(literal);
		if (out_error != NULL && out_error->code == SQLPARSER_STATUS_OK) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		}
		return NULL;
	}
	return object;
}

static json_t *sqlparser_graph_dml_assignment_json(
	const sqlparser_query_graph_view_t *graph,
	const sqlparser_graph_dml_assignment_t *assignment,
	sqlparser_error_t *out_error)
{
	json_t *object;
	json_t *literal;
	json_t *rhs_fields;
	json_t *rhs_values;
	json_t *rhs_blocks;

	object = json_object();
	literal = sqlparser_graph_literal_json(
		&assignment->literal,
		assignment->value_kind == SQLPARSER_GRAPH_VALUE_LITERAL);
	rhs_fields = sqlparser_graph_span_json(
		graph,
		assignment->rhs_fields,
		out_error);
	rhs_values = sqlparser_graph_span_json(
		graph,
		assignment->rhs_values,
		out_error);
	rhs_blocks = sqlparser_graph_span_json(
		graph,
		assignment->rhs_blocks,
		out_error);
	if (object == NULL || literal == NULL || rhs_fields == NULL ||
	    rhs_values == NULL || rhs_blocks == NULL ||
	    json_object_set_new(object, "target_field", json_integer((json_int_t)assignment->target_field_index)) != 0 ||
	    json_object_set_new(object, "kind", json_string(sqlparser_graph_value_kind_name(assignment->value_kind))) != 0 ||
	    sqlparser_json_set_optional_size(object, "source_target", assignment->has_source_target, assignment->source_target_index) != 0 ||
	    sqlparser_json_set_optional_size(object, "source_field", assignment->has_source_field, assignment->source_field_index) != 0 ||
	    sqlparser_json_set_nonempty_array(object, "rhs_fields", &rhs_fields) != 0 ||
	    sqlparser_json_set_nonempty_array(object, "rhs_values", &rhs_values) != 0 ||
	    sqlparser_json_set_nonempty_array(object, "rhs_blocks", &rhs_blocks) != 0 ||
	    sqlparser_json_set_optional_string(object, "bind_key", assignment->has_bind ? assignment->bind : NULL) != 0 ||
	    json_object_set_new(object, "bind_kind", json_integer(assignment->bind_kind)) != 0 ||
	    sqlparser_json_set_optional_string(object, "bind_sql", assignment->has_bind_sql ? assignment->bind_sql : NULL) != 0 ||
	    sqlparser_json_set_optional_size(object, "bind_position", assignment->has_bind_position, assignment->bind_position) != 0 ||
	    sqlparser_json_set_optional_selector(object, "selector", assignment->has_selector ? &assignment->selector : NULL, out_error) != 0) {
		json_decref(object);
		json_decref(literal);
		json_decref(rhs_fields);
		json_decref(rhs_values);
		json_decref(rhs_blocks);
		if (out_error != NULL && out_error->code == SQLPARSER_STATUS_OK) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		}
		return NULL;
	}
	if (json_is_null(literal)) {
		json_decref(literal);
		literal = NULL;
	} else if (sqlparser_json_object_set_owned(object, "literal", &literal) != 0) {
		json_decref(object);
		json_decref(literal);
		json_decref(rhs_fields);
		json_decref(rhs_values);
		json_decref(rhs_blocks);
		if (out_error != NULL && out_error->code == SQLPARSER_STATUS_OK) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		}
		return NULL;
	}
	return object;
}

static int sqlparser_graph_append_dml_column_objects(
	const sqlparser_query_graph_view_t *graph,
	json_t *array,
	sqlparser_index_span_t span,
	sqlparser_error_t *out_error)
{
	size_t index;

	for (index = 0U; index < span.count; index++) {
		size_t column_index;
		sqlparser_graph_dml_column_t column;
		json_t *entry;

		if (sqlparser_query_graph_span_index_at(graph, span, index, &column_index, out_error) != SQLPARSER_STATUS_OK ||
		    sqlparser_query_graph_dml_column_at(graph, column_index, &column, out_error) != SQLPARSER_STATUS_OK) {
			return -1;
		}
		entry = sqlparser_graph_dml_column_json(&column, out_error);
		if (entry == NULL || sqlparser_json_array_append_owned(array, &entry) != 0) {
			json_decref(entry);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return -1;
		}
	}
	return 0;
}

static int sqlparser_graph_append_dml_cell_objects(
	const sqlparser_query_graph_view_t *graph,
	json_t *array,
	sqlparser_index_span_t span,
	sqlparser_error_t *out_error)
{
	size_t index;

	for (index = 0U; index < span.count; index++) {
		size_t cell_index;
		sqlparser_graph_dml_cell_t cell;
		json_t *entry;

		if (sqlparser_query_graph_span_index_at(graph, span, index, &cell_index, out_error) != SQLPARSER_STATUS_OK ||
		    sqlparser_query_graph_dml_cell_at(graph, cell_index, &cell, out_error) != SQLPARSER_STATUS_OK) {
			return -1;
		}
		entry = sqlparser_graph_dml_cell_json(graph, &cell, out_error);
		if (entry == NULL || sqlparser_json_array_append_owned(array, &entry) != 0) {
			json_decref(entry);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return -1;
		}
	}
	return 0;
}

static int sqlparser_graph_append_dml_assignment_objects(
	const sqlparser_query_graph_view_t *graph,
	json_t *array,
	sqlparser_index_span_t span,
	sqlparser_error_t *out_error)
{
	size_t index;

	for (index = 0U; index < span.count; index++) {
		size_t assignment_index;
		sqlparser_graph_dml_assignment_t assignment;
		json_t *entry;

		if (sqlparser_query_graph_span_index_at(graph, span, index, &assignment_index, out_error) != SQLPARSER_STATUS_OK ||
		    sqlparser_query_graph_dml_assignment_at(graph, assignment_index, &assignment, out_error) != SQLPARSER_STATUS_OK) {
			return -1;
		}
		entry = sqlparser_graph_dml_assignment_json(graph, &assignment, out_error);
		if (entry == NULL || sqlparser_json_array_append_owned(array, &entry) != 0) {
			json_decref(entry);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return -1;
		}
	}
	return 0;
}

static json_t *sqlparser_graph_dml_branch_json(
	const sqlparser_query_graph_view_t *graph,
	const sqlparser_graph_dml_branch_t *branch,
	sqlparser_error_t *out_error)
{
	const sqlparser_graph_merge_branch_detail_t *detail;
	const sqlparser_selector_t *target_list_selector_ptr;
	json_t *object;
	json_t *assignments;
	json_t *target_columns;
	json_t *rows;
	const char *merge_action_kind;
	const char *merge_match_kind;
	sqlparser_selector_t target_list_selector;

	object = json_object();
	assignments = json_array();
	target_columns = json_array();
	rows = json_array();
	if (object == NULL || assignments == NULL ||
	    target_columns == NULL || rows == NULL) {
		goto fail;
	}
	detail = sqlparser_query_graph_merge_branch_detail_entry(
		graph,
		branch->index);
	memset(&target_list_selector, 0, sizeof(target_list_selector));
	target_list_selector_ptr = NULL;
	if (detail != NULL &&
	    detail->action_kind == SQLPARSER_GRAPH_MERGE_ACTION_INSERT &&
	    branch->target_columns.count > 0U) {
		target_list_selector.kind =
			SQLPARSER_SELECTOR_KIND_INSERT_BRANCH_COLUMNS;
		target_list_selector.statement_index = branch->statement_index;
		target_list_selector.row_index = branch->dml_index;
		target_list_selector.item_index = branch->ordinal;
		target_list_selector_ptr = &target_list_selector;
	}
	merge_action_kind =
		detail != NULL &&
		detail->action_kind !=
				SQLPARSER_GRAPH_MERGE_ACTION_UNKNOWN ?
			sqlparser_graph_merge_action_kind_name(
				detail->action_kind) :
			NULL;
	merge_match_kind =
		detail != NULL &&
		detail->match_kind !=
				SQLPARSER_GRAPH_MERGE_MATCH_UNKNOWN ?
			sqlparser_graph_merge_match_kind_name(
				detail->match_kind) :
			NULL;
	if (sqlparser_graph_append_dml_column_objects(graph, target_columns, branch->target_columns, out_error) != 0 ||
	    sqlparser_graph_append_dml_cell_objects(graph, rows, branch->rows, out_error) != 0 ||
	    (detail != NULL &&
	     sqlparser_graph_append_dml_assignment_objects(
		     graph,
		     assignments,
		     detail->assignments,
		     out_error) != 0) ||
	    json_object_set_new(object, "ordinal", json_integer((json_int_t)branch->ordinal)) != 0 ||
	    json_object_set_new(object, "branch_kind", json_string(sqlparser_graph_dml_branch_kind_name(branch->branch_kind))) != 0 ||
	    sqlparser_json_set_optional_string(object, "merge_action_kind", merge_action_kind) != 0 ||
	    sqlparser_json_set_optional_string(object, "merge_match_kind", merge_match_kind) != 0 ||
	    sqlparser_json_set_optional_size(object, "target_relation", branch->has_target_relation, branch->target_relation_index) != 0 ||
	    sqlparser_json_set_optional_selector(object, "target_list_selector", target_list_selector_ptr, out_error) != 0 ||
	    sqlparser_json_set_nonempty_array(object, "target_columns", &target_columns) != 0 ||
	    sqlparser_json_set_nonempty_array(object, "rows", &rows) != 0 ||
	    sqlparser_json_set_nonempty_array(object, "assignments", &assignments) != 0 ||
	    sqlparser_json_set_optional_size(object, "condition_block", branch->has_condition_block, branch->condition_block_index) != 0 ||
	    sqlparser_json_set_optional_selector(object, "condition_selector", branch->has_condition_selector ? &branch->condition_selector : NULL, out_error) != 0 ||
	    sqlparser_json_set_optional_selector(
		    object,
		    "delete_condition_selector",
		    branch->has_delete_condition_selector ?
			    &branch->delete_condition_selector :
			    NULL,
		    out_error) != 0) {
		goto fail;
	}
	return object;

fail:
	json_decref(object);
	json_decref(assignments);
	json_decref(target_columns);
	json_decref(rows);
	if (out_error != NULL && out_error->code == SQLPARSER_STATUS_OK) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
	}
	return NULL;
}

static int sqlparser_graph_append_dml_branch_objects(
	const sqlparser_query_graph_view_t *graph,
	json_t *array,
	sqlparser_index_span_t span,
	sqlparser_error_t *out_error)
{
	size_t index;

	for (index = 0U; index < span.count; index++) {
		size_t branch_index;
		sqlparser_graph_dml_branch_t branch;
		json_t *entry;

		if (sqlparser_query_graph_span_index_at(graph, span, index, &branch_index, out_error) != SQLPARSER_STATUS_OK ||
		    sqlparser_query_graph_dml_branch_at(graph, branch_index, &branch, out_error) != SQLPARSER_STATUS_OK) {
			return -1;
		}
		entry = sqlparser_graph_dml_branch_json(graph, &branch, out_error);
		if (entry == NULL || sqlparser_json_array_append_owned(array, &entry) != 0) {
			json_decref(entry);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return -1;
		}
	}
	return 0;
}

static const char *sqlparser_graph_dml_result_kind_json_name(
	sqlparser_graph_dml_result_kind_t kind)
{
	return kind == SQLPARSER_GRAPH_DML_RESULT_SINK ? "sink" : "client";
}

static const char *sqlparser_graph_dml_reference_kind_json_name(
	sqlparser_graph_dml_reference_kind_t kind)
{
	switch (kind) {
		case SQLPARSER_GRAPH_DML_REFERENCE_TARGET_BEFORE:
			return "target_before";
		case SQLPARSER_GRAPH_DML_REFERENCE_TARGET_AFTER:
			return "target_after";
		case SQLPARSER_GRAPH_DML_REFERENCE_SOURCE:
		default:
			return "source";
	}
}

static json_t *sqlparser_graph_dml_reference_json(
	const sqlparser_graph_dml_reference_t *reference,
	sqlparser_error_t *out_error)
{
	json_t *object;

	object = json_object();
	if (object == NULL ||
	    sqlparser_json_set_size(object, "target", reference->target_index) != 0 ||
	    sqlparser_json_set_optional_size(object, "field", reference->has_field, reference->field_index) != 0 ||
	    json_object_set_new(
		    object,
		    "kind",
		    json_string(sqlparser_graph_dml_reference_kind_json_name(reference->kind))) != 0 ||
	    sqlparser_json_set_size(object, "relation", reference->relation_index) != 0) {
		json_decref(object);
		if (out_error != NULL && out_error->code == SQLPARSER_STATUS_OK) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		}
		return NULL;
	}
	return object;
}

static json_t *sqlparser_graph_dml_result_json(
	const sqlparser_query_graph_view_t *graph,
	const sqlparser_graph_dml_result_t *result,
	sqlparser_error_t *out_error)
{
	json_t *object;
	json_t *sink_columns;
	json_t *references;
	size_t index;

	object = json_object();
	sink_columns = json_array();
	references = json_array();
	if (object == NULL || sink_columns == NULL || references == NULL ||
	    sqlparser_graph_append_dml_column_objects(graph, sink_columns, result->sink_columns, out_error) != 0) {
		goto fail;
	}
	for (index = 0U; index < result->references.count; index++) {
		size_t reference_index;
		sqlparser_graph_dml_reference_t reference;
		json_t *entry;

		if (sqlparser_query_graph_span_index_at(
			    graph,
			    result->references,
			    index,
			    &reference_index,
			    out_error) != SQLPARSER_STATUS_OK ||
		    sqlparser_query_graph_dml_reference_at(
			    graph,
			    reference_index,
			    &reference,
			    out_error) != SQLPARSER_STATUS_OK) {
			goto fail;
		}
		entry = sqlparser_graph_dml_reference_json(&reference, out_error);
		if (entry == NULL || sqlparser_json_array_append_owned(references, &entry) != 0) {
			json_decref(entry);
			goto fail;
		}
	}
	if (json_object_set_new(
		    object,
		    "kind",
		    json_string(sqlparser_graph_dml_result_kind_json_name(result->kind))) != 0 ||
	    sqlparser_json_set_size(object, "block", result->block_index) != 0 ||
	    sqlparser_json_set_optional_size(
		    object,
		    "sink_relation",
		    result->has_sink_relation,
		    result->sink_relation_index) != 0 ||
	    sqlparser_json_set_nonempty_array(object, "sink_columns", &sink_columns) != 0 ||
	    sqlparser_json_set_nonempty_array(object, "references", &references) != 0) {
		goto fail;
	}
	return object;

fail:
	json_decref(object);
	json_decref(sink_columns);
	json_decref(references);
	if (out_error != NULL && out_error->code == SQLPARSER_STATUS_OK) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
	}
	return NULL;
}

static json_t *sqlparser_graph_dml_json(
	const sqlparser_query_graph_view_t *graph,
	const sqlparser_graph_dml_t *dml,
	const size_t *first_child,
	const size_t *next_sibling,
	size_t dml_count,
	size_t depth,
	sqlparser_error_t *out_error)
{
	json_t *object;
	json_t *target_columns;
	json_t *rows;
	json_t *assignments;
	json_t *delete_targets;
	json_t *branches;
	json_t *result_channels;
	json_t *children;
	size_t result_count;
	size_t result_index;
	size_t child_index;

	object = json_object();
	target_columns = json_array();
	rows = json_array();
	assignments = json_array();
	delete_targets = sqlparser_graph_span_json(graph, dml->delete_targets, out_error);
	branches = json_array();
	result_channels = json_array();
	children = json_array();
	if (object == NULL || target_columns == NULL || rows == NULL || assignments == NULL ||
	    delete_targets == NULL || branches == NULL || result_channels == NULL || children == NULL) {
		goto fail;
	}
	if (dml_count == 0U || dml->index >= dml_count || depth >= dml_count ||
	    first_child == NULL || next_sibling == NULL) {
		if (out_error != NULL && out_error->code == SQLPARSER_STATUS_OK) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "DML parent relationship contains a cycle");
		}
		goto fail;
	}
	result_count = 0U;
	if (sqlparser_query_graph_dml_result_count(
		    graph,
		    dml->index,
		    &result_count,
		    out_error) != SQLPARSER_STATUS_OK) {
		goto fail;
	}
	for (result_index = 0U; result_index < result_count; result_index++) {
		sqlparser_graph_dml_result_t result;
		json_t *entry;

		if (sqlparser_query_graph_dml_result_at(
			    graph,
			    dml->index,
			    result_index,
			    &result,
			    out_error) != SQLPARSER_STATUS_OK) {
			goto fail;
		}
		entry = sqlparser_graph_dml_result_json(graph, &result, out_error);
		if (entry == NULL || sqlparser_json_array_append_owned(result_channels, &entry) != 0) {
			json_decref(entry);
			goto fail;
		}
	}
	if (sqlparser_graph_append_dml_column_objects(graph, target_columns, dml->target_columns, out_error) != 0 ||
	    sqlparser_graph_append_dml_cell_objects(graph, rows, dml->rows, out_error) != 0 ||
	    sqlparser_graph_append_dml_assignment_objects(graph, assignments, dml->assignments, out_error) != 0 ||
	    sqlparser_graph_append_dml_branch_objects(graph, branches, dml->branches, out_error) != 0 ||
	    json_object_set_new(object, "kind", json_string(sqlparser_graph_dml_kind_name(dml->kind))) != 0 ||
	    json_object_set_new(object, "insert_mode", json_string(sqlparser_graph_insert_mode_name(dml->insert_mode))) != 0 ||
	    sqlparser_json_set_optional_size(object, "target_relation", dml->has_target_relation, dml->target_relation_index) != 0) {
		goto fail;
	}
	if (sqlparser_json_set_nonempty_array(object, "target_columns", &target_columns) != 0) {
		goto fail;
	}
	if (sqlparser_json_set_nonempty_array(object, "rows", &rows) != 0) {
		goto fail;
	}
	if (sqlparser_json_set_nonempty_array(object, "assignments", &assignments) != 0) {
		goto fail;
	}
	if (sqlparser_json_set_nonempty_array(object, "delete_targets", &delete_targets) != 0) {
		goto fail;
	}
	if (sqlparser_json_set_nonempty_array(object, "branches", &branches) != 0) {
		goto fail;
	}
	if (sqlparser_json_set_nonempty_array(object, "result_channels", &result_channels) != 0) {
		goto fail;
	}
	child_index = first_child[dml->index];
	while (child_index != SIZE_MAX) {
		sqlparser_graph_dml_t child;
		json_t *entry;

		if (child_index >= dml_count) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "DML child index is invalid");
			goto fail;
		}
		if (sqlparser_query_graph_dml_at(graph, child_index, &child, out_error) != SQLPARSER_STATUS_OK) {
			goto fail;
		}
		entry = sqlparser_graph_dml_json(
			graph,
			&child,
			first_child,
			next_sibling,
			dml_count,
			depth + 1U,
			out_error);
		if (entry == NULL || sqlparser_json_array_append_owned(children, &entry) != 0) {
			json_decref(entry);
			goto fail;
		}
		child_index = next_sibling[child_index];
	}
	if (sqlparser_json_set_nonempty_array(object, "children", &children) != 0) {
		goto fail;
	}
	if (sqlparser_json_set_optional_size(object, "source_block", dml->has_source_block, dml->source_block_index) != 0) {
		goto fail;
	}
	return object;

fail:
	json_decref(object);
	json_decref(target_columns);
	json_decref(rows);
	json_decref(assignments);
	json_decref(delete_targets);
	json_decref(branches);
	json_decref(result_channels);
	json_decref(children);
	if (out_error != NULL && out_error->code == SQLPARSER_STATUS_OK) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
	}
	return NULL;
}

static json_t *sqlparser_graph_json_from_view(
	const sqlparser_handle_t *handle,
	const sqlparser_query_graph_view_t *graph,
	sqlparser_error_t *out_error)
{
	json_t *object;
	json_t *blocks;
	json_t *relations;
	json_t *targets;
	json_t *fields;
	json_t *values;
	json_t *sets;
	json_t *predicates;
	json_t *session;
	size_t *dml_child_links;
	size_t index;

	dml_child_links = NULL;
	object = json_object();
	blocks = json_array();
	relations = json_array();
	targets = json_array();
	fields = json_array();
	values = json_array();
	sets = json_array();
	predicates = json_array();
	session = sqlparser_graph_session_json(graph, out_error);
	if (object == NULL || blocks == NULL || relations == NULL || targets == NULL ||
	    fields == NULL || values == NULL || sets == NULL || predicates == NULL || session == NULL ||
	    sqlparser_json_set_optional_size(object, "root", graph->has_root_block, graph->root_block_index) != 0) {
		goto fail;
	}
	for (index = 0U; index < graph->block_count; index++) {
		sqlparser_graph_block_t block;
		json_t *entry;
		if (sqlparser_query_graph_block_at(graph, index, &block, out_error) != SQLPARSER_STATUS_OK) {
			goto fail;
		}
		entry = sqlparser_graph_block_json(graph, &block, out_error);
		if (entry == NULL || sqlparser_json_array_append_owned(blocks, &entry) != 0) {
			json_decref(entry);
			goto fail;
		}
	}
	for (index = 0U; index < graph->relation_count; index++) {
		sqlparser_graph_relation_t relation;
		json_t *entry;
		if (sqlparser_query_graph_relation_at(graph, index, &relation, out_error) != SQLPARSER_STATUS_OK) {
			goto fail;
		}
		entry = sqlparser_graph_relation_json(&relation, out_error);
		if (entry == NULL || sqlparser_json_array_append_owned(relations, &entry) != 0) {
			json_decref(entry);
			goto fail;
		}
	}
	for (index = 0U; index < graph->target_count; index++) {
		sqlparser_graph_target_t target;
		json_t *entry;
		if (sqlparser_query_graph_target_at(graph, index, &target, out_error) != SQLPARSER_STATUS_OK) {
			goto fail;
		}
		entry = sqlparser_graph_target_json(graph, &target, out_error);
		if (entry == NULL || sqlparser_json_array_append_owned(targets, &entry) != 0) {
			json_decref(entry);
			goto fail;
		}
	}
	for (index = 0U; index < graph->field_count; index++) {
		sqlparser_graph_field_t field;
		json_t *entry;
		if (sqlparser_query_graph_field_at(graph, index, &field, out_error) != SQLPARSER_STATUS_OK) {
			goto fail;
		}
		entry = sqlparser_graph_field_json(graph, &field, out_error);
		if (entry == NULL || sqlparser_json_array_append_owned(fields, &entry) != 0) {
			json_decref(entry);
			goto fail;
		}
	}
	for (index = 0U; index < graph->value_count; index++) {
		sqlparser_graph_value_t value;
		json_t *entry;
		if (sqlparser_query_graph_value_at(graph, index, &value, out_error) != SQLPARSER_STATUS_OK) {
			goto fail;
		}
		entry = sqlparser_graph_value_json(graph, &value, out_error);
		if (entry == NULL || sqlparser_json_array_append_owned(values, &entry) != 0) {
			json_decref(entry);
			goto fail;
		}
	}
	for (index = 0U; index < graph->set_count; index++) {
		sqlparser_graph_set_t set_item;
		json_t *entry;
		if (sqlparser_query_graph_set_at(graph, index, &set_item, out_error) != SQLPARSER_STATUS_OK) {
			goto fail;
		}
		entry = sqlparser_graph_set_json(handle, graph, &set_item, out_error);
		if (entry == NULL || sqlparser_json_array_append_owned(sets, &entry) != 0) {
			json_decref(entry);
			goto fail;
		}
	}
	for (index = 0U; index < graph->predicate_count; index++) {
		sqlparser_graph_predicate_t predicate;
		json_t *entry;
		if (sqlparser_query_graph_predicate_at(graph, index, &predicate, out_error) != SQLPARSER_STATUS_OK) {
			goto fail;
		}
		entry = sqlparser_graph_predicate_json(graph, &predicate, out_error);
		if (entry == NULL || sqlparser_json_array_append_owned(predicates, &entry) != 0) {
			json_decref(entry);
			goto fail;
		}
	}
	if (sqlparser_json_set_nonempty_array(object, "blocks", &blocks) != 0 ||
	    sqlparser_json_set_nonempty_array(object, "relations", &relations) != 0 ||
	    sqlparser_json_set_nonempty_array(object, "targets", &targets) != 0 ||
	    sqlparser_json_set_nonempty_array(object, "fields", &fields) != 0 ||
	    sqlparser_json_set_nonempty_array(object, "values", &values) != 0 ||
	    sqlparser_json_set_nonempty_array(object, "sets", &sets) != 0 ||
	    sqlparser_json_set_nonempty_array(object, "predicates", &predicates) != 0) {
		goto fail;
	}
	if (json_is_null(session)) {
		json_decref(session);
		session = NULL;
	} else if (sqlparser_json_object_set_owned(object, "session", &session) != 0) {
		goto fail;
	}
	if (graph->has_dml) {
		json_t *dmls;
		size_t *first_child;
		size_t *next_sibling;
		size_t dml_count;
		size_t root_count;
		size_t root_index;

		dmls = json_array();
		dml_count = 0U;
		root_count = 0U;
		root_index = 0U;
		if (dmls == NULL ||
		    sqlparser_query_graph_dml_count(
			    graph, &dml_count, out_error) != SQLPARSER_STATUS_OK) {
			json_decref(dmls);
			goto fail;
		}
		if (dml_count == 0U ||
		    dml_count > SIZE_MAX / sizeof(*dml_child_links) / 2U) {
			json_decref(dmls);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_RESOURCE_LIMIT, "DML hierarchy is too large");
			goto fail;
		}
		dml_child_links = (size_t *)malloc(
			2U * dml_count * sizeof(*dml_child_links));
		if (dml_child_links == NULL) {
			json_decref(dmls);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			goto fail;
		}
		first_child = dml_child_links;
		next_sibling = dml_child_links + dml_count;
		for (index = 0U; index < dml_count; index++) {
			first_child[index] = SIZE_MAX;
			next_sibling[index] = SIZE_MAX;
		}
		for (index = dml_count; index > 0U; index--) {
			size_t child_index;
			size_t parent_index;
			int has_parent;

			child_index = index - 1U;
			if (sqlparser_query_graph_dml_parent(
				    graph,
				    child_index,
				    &parent_index,
				    &has_parent,
				    out_error) != SQLPARSER_STATUS_OK) {
				json_decref(dmls);
				goto fail;
			}
			if (has_parent) {
				if (parent_index >= child_index) {
					json_decref(dmls);
					sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "DML parent index is invalid");
					goto fail;
				}
				next_sibling[child_index] = first_child[parent_index];
				first_child[parent_index] = child_index;
			} else {
				next_sibling[child_index] = dml_count;
				root_index = child_index;
				root_count++;
			}
		}
		if (root_count == 1U) {
			sqlparser_graph_dml_t dml;
			json_t *entry;

			json_decref(dmls);
			dmls = NULL;
			if (sqlparser_query_graph_dml_at(
				    graph,
				    root_index,
				    &dml,
				    out_error) != SQLPARSER_STATUS_OK) {
				goto fail;
			}
			entry = sqlparser_graph_dml_json(
				graph,
				&dml,
				first_child,
				next_sibling,
				dml_count,
				0U,
				out_error);
			if (entry == NULL ||
			    sqlparser_json_object_set_owned(object, "dml", &entry) != 0) {
				json_decref(entry);
				goto fail;
			}
		} else if (root_count > 1U) {
			for (index = 0U; index < dml_count; index++) {
				sqlparser_graph_dml_t dml;
				json_t *entry;

				if (next_sibling[index] != dml_count) {
					continue;
				}
				if (sqlparser_query_graph_dml_at(
					    graph,
					    index,
					    &dml,
					    out_error) != SQLPARSER_STATUS_OK) {
					json_decref(dmls);
					goto fail;
				}
				entry = sqlparser_graph_dml_json(
					graph,
					&dml,
					first_child,
					next_sibling,
					dml_count,
					0U,
					out_error);
				if (entry == NULL ||
				    sqlparser_json_array_append_owned(dmls, &entry) != 0) {
					json_decref(entry);
					json_decref(dmls);
					goto fail;
				}
			}
			if (sqlparser_json_object_set_owned(object, "dmls", &dmls) != 0) {
				json_decref(dmls);
				goto fail;
			}
		} else {
			json_decref(dmls);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "DML parent relationship has no root");
			goto fail;
		}
		free(dml_child_links);
		dml_child_links = NULL;
	}
	return object;

fail:
	json_decref(object);
	json_decref(blocks);
	json_decref(relations);
	json_decref(targets);
	json_decref(fields);
	json_decref(values);
	json_decref(sets);
	json_decref(predicates);
	json_decref(session);
	free(dml_child_links);
	if (out_error != NULL && out_error->code == SQLPARSER_STATUS_OK) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
	}
	return NULL;
}

static json_t *sqlparser_control_items_json(
	const sqlparser_control_flow_view_t *flow,
	sqlparser_index_span_t span,
	sqlparser_error_t *out_error)
{
	json_t *items;
	size_t ordinal;

	items = json_array();
	if (items == NULL) {
		return NULL;
	}
	for (ordinal = 0U; ordinal < span.count; ordinal++) {
		sqlparser_control_item_t item;
		json_t *entry;
		size_t item_index;

		memset(&item, 0, sizeof(item));
		if (sqlparser_control_span_index_at(flow, span, ordinal, &item_index, out_error) != SQLPARSER_STATUS_OK ||
		    sqlparser_control_item_at(flow, item_index, &item, out_error) != SQLPARSER_STATUS_OK) {
			json_decref(items);
			return NULL;
		}
		entry = json_object();
		if (entry == NULL ||
		    json_object_set_new(
			    entry,
			    "kind",
			    json_string(item.kind == SQLPARSER_CONTROL_ITEM_NODE ? "node" : "statement")) != 0 ||
		    sqlparser_json_set_size(entry, "index", item.index) != 0 ||
		    sqlparser_json_array_append_owned(items, &entry) != 0) {
			json_decref(entry);
			json_decref(items);
			if (out_error != NULL && out_error->code == SQLPARSER_STATUS_OK) {
				sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			}
			return NULL;
		}
	}
	return items;
}

static sqlparser_status_t sqlparser_control_flow_json(
	const sqlparser_handle_t *handle,
	json_t **out_json,
	sqlparser_error_t *out_error)
{
	sqlparser_control_flow_view_t flow;
	json_t *object;
	json_t *roots;
	json_t *nodes;
	json_t *branches;
	size_t index;
	sqlparser_status_t status;

	if (out_json == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "control JSON output must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_json = NULL;
	memset(&flow, 0, sizeof(flow));
	status = sqlparser_handle_control_flow(handle, &flow, out_error);
	if (status != SQLPARSER_STATUS_OK || flow.node_count == 0U) {
		return status;
	}

	object = json_object();
	roots = sqlparser_control_items_json(&flow, flow.roots, out_error);
	nodes = json_array();
	branches = json_array();
	if (object == NULL || roots == NULL || nodes == NULL || branches == NULL) {
		goto fail;
	}
	for (index = 0U; index < flow.node_count; index++) {
		sqlparser_control_node_t node;
		json_t *entry;
		json_t *branch_indices;
		size_t ordinal;

		memset(&node, 0, sizeof(node));
		if (sqlparser_control_node_at(&flow, index, &node, out_error) != SQLPARSER_STATUS_OK) {
			goto fail;
		}
		entry = json_object();
		branch_indices = json_array();
		if (entry == NULL || branch_indices == NULL ||
		    json_object_set_new(entry, "kind", json_string("if")) != 0) {
			json_decref(entry);
			json_decref(branch_indices);
			goto fail;
		}
		for (ordinal = 0U; ordinal < node.branches.count; ordinal++) {
			size_t branch_index;
			json_t *branch_value;

			if (sqlparser_control_span_index_at(
				    &flow, node.branches, ordinal, &branch_index, out_error) != SQLPARSER_STATUS_OK) {
				json_decref(entry);
				json_decref(branch_indices);
				goto fail;
			}
			branch_value = json_integer((json_int_t)branch_index);
			if (branch_value == NULL ||
			    sqlparser_json_array_append_owned(branch_indices, &branch_value) != 0) {
				json_decref(branch_value);
				json_decref(entry);
				json_decref(branch_indices);
				goto fail;
			}
		}
		if (sqlparser_json_object_set_owned(entry, "branches", &branch_indices) != 0 ||
		    sqlparser_json_array_append_owned(nodes, &entry) != 0) {
			json_decref(entry);
			json_decref(branch_indices);
			goto fail;
		}
	}
	for (index = 0U; index < flow.branch_count; index++) {
		sqlparser_control_branch_t branch;
		json_t *entry;
		json_t *items;

		memset(&branch, 0, sizeof(branch));
		if (sqlparser_control_branch_at(&flow, index, &branch, out_error) != SQLPARSER_STATUS_OK) {
			goto fail;
		}
		entry = json_object();
		items = sqlparser_control_items_json(&flow, branch.items, out_error);
		if (entry == NULL || items == NULL ||
		    sqlparser_json_set_optional_size(
			    entry,
			    "condition_statement",
			    branch.has_condition,
			    branch.condition_statement_index) != 0 ||
		    sqlparser_json_object_set_owned(entry, "items", &items) != 0 ||
		    sqlparser_json_array_append_owned(branches, &entry) != 0) {
			json_decref(entry);
			json_decref(items);
			goto fail;
		}
	}
	if (sqlparser_json_object_set_owned(object, "roots", &roots) != 0 ||
	    sqlparser_json_object_set_owned(object, "nodes", &nodes) != 0 ||
	    sqlparser_json_object_set_owned(object, "branches", &branches) != 0) {
		goto fail;
	}
	*out_json = object;
	return SQLPARSER_STATUS_OK;

fail:
	json_decref(object);
	json_decref(roots);
	json_decref(nodes);
	json_decref(branches);
	if (out_error != NULL && out_error->code == SQLPARSER_STATUS_OK) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
	}
	return out_error != NULL ? out_error->code : SQLPARSER_STATUS_INTERNAL_ERROR;
}

sqlparser_status_t sqlparser_export_view_json(
	const sqlparser_handle_t *handle,
	int pretty,
	char **out_json,
	sqlparser_error_t *out_error)
{
	json_t *root;
	json_t *statements;
	sqlparser_handle_t *mutable_handle;
	size_t statement_index;
	sqlparser_status_t status;
	char *json_text;
	json_t *control_flow;
	int ast_was_loaded;

	if (out_json == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "out_json must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_json = NULL;
	sqlparser_error_clear(out_error);
	if (handle == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "handle must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	mutable_handle = (sqlparser_handle_t *)handle;
	ast_was_loaded = mutable_handle->ast != NULL;
	status = sqlparser_handle_ensure_ast(mutable_handle, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	root = json_object();
	statements = json_array();
	if (root == NULL || statements == NULL ||
	    sqlparser_json_object_set_owned(root, "statements", &statements) != 0) {
		json_decref(root);
		json_decref(statements);
		if (!ast_was_loaded) {
			sqlparser_handle_clear_ast(mutable_handle);
		}
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	statements = NULL;

	for (statement_index = 0U; statement_index < handle->statement_count; statement_index++) {
		PgQuery__Node *statement_node;
		sqlparser_query_graph_view_t graph;
		json_t *statement_json;
		json_t *graph_json;
		const char *keyword;

		statement_node = NULL;
		status = sqlparser_get_statement_node(mutable_handle, statement_index, &statement_node, out_error);
		if (status != SQLPARSER_STATUS_OK) {
			json_decref(root);
			if (!ast_was_loaded) {
				sqlparser_handle_clear_query_graph(mutable_handle);
				sqlparser_handle_clear_ast(mutable_handle);
			}
			return status;
		}
		keyword = sqlparser_statement_keyword_for_handle(handle, statement_index, statement_node);
		statement_json = json_object();
		if (statement_json == NULL ||
		    sqlparser_json_set_size(statement_json, "index", statement_index) != 0 ||
		    json_object_set_new(statement_json, "keyword", json_string(keyword != NULL ? keyword : "")) != 0) {
			json_decref(statement_json);
			json_decref(root);
			if (!ast_was_loaded) {
				sqlparser_handle_clear_query_graph(mutable_handle);
				sqlparser_handle_clear_ast(mutable_handle);
			}
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}

		memset(&graph, 0, sizeof(graph));
		status = sqlparser_statement_query_graph(handle, statement_index, &graph, out_error);
		if (status != SQLPARSER_STATUS_OK) {
			json_decref(statement_json);
			json_decref(root);
			if (!ast_was_loaded) {
				sqlparser_handle_clear_query_graph(mutable_handle);
				sqlparser_handle_clear_ast(mutable_handle);
			}
			return status;
		}
		graph_json = sqlparser_graph_json_from_view(handle, &graph, out_error);
		if (graph_json == NULL ||
		    sqlparser_json_object_set_owned(statement_json, "query_graph", &graph_json) != 0) {
			json_decref(graph_json);
			json_decref(statement_json);
			json_decref(root);
			if (!ast_was_loaded) {
				sqlparser_handle_clear_query_graph(mutable_handle);
				sqlparser_handle_clear_ast(mutable_handle);
			}
			if (out_error != NULL && out_error->code == SQLPARSER_STATUS_OK) {
				sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			}
			return out_error != NULL ? out_error->code : SQLPARSER_STATUS_INTERNAL_ERROR;
		}

		if (sqlparser_json_array_append_owned(json_object_get(root, "statements"), &statement_json) != 0) {
			json_decref(statement_json);
			json_decref(root);
			if (!ast_was_loaded) {
				sqlparser_handle_clear_query_graph(mutable_handle);
				sqlparser_handle_clear_ast(mutable_handle);
			}
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
	}
	control_flow = NULL;
	status = sqlparser_control_flow_json(handle, &control_flow, out_error);
	if (status != SQLPARSER_STATUS_OK ||
	    (control_flow != NULL && sqlparser_json_object_set_owned(root, "control_flow", &control_flow) != 0)) {
		json_decref(control_flow);
		json_decref(root);
		if (!ast_was_loaded) {
			sqlparser_handle_clear_query_graph(mutable_handle);
			sqlparser_handle_clear_ast(mutable_handle);
		}
		if (out_error != NULL && out_error->code == SQLPARSER_STATUS_OK) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		}
		return out_error != NULL ? out_error->code : SQLPARSER_STATUS_INTERNAL_ERROR;
	}

	json_text = json_dumps(root, (pretty ? JSON_INDENT(2) : JSON_COMPACT) | JSON_ENSURE_ASCII);
	json_decref(root);
	if (json_text == NULL) {
		if (!ast_was_loaded) {
			sqlparser_handle_clear_query_graph(mutable_handle);
			sqlparser_handle_clear_ast(mutable_handle);
		}
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	status = sqlparser_validate_handle_output_text(handle, json_text, "View JSON", out_error);
	if (status != SQLPARSER_STATUS_OK) {
		free(json_text);
		if (!ast_was_loaded) {
			sqlparser_handle_clear_query_graph(mutable_handle);
			sqlparser_handle_clear_ast(mutable_handle);
		}
		return status;
	}

	*out_json = json_text;
	if (!ast_was_loaded) {
		sqlparser_handle_clear_query_graph(mutable_handle);
		sqlparser_handle_clear_ast(mutable_handle);
	}
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_clause_sql(
	const sqlparser_clause_view_t *clause,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	sqlparser_error_clear(out_error);
	if (clause == NULL || clause->handle == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "clause must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	return sqlparser_view_full_clause_sql(
		clause->handle,
		clause->statement_index,
		clause->clause_index,
		out_sql,
		out_error);
}
