#include <ctype.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "sqlparser_ast_internal.h"
#include "../dialect/sqlparser_dialect_dml_result_internal.h"

static int sqlparser_optional_relation_name_equal(
	const char *left,
	const char *right)
{
	const unsigned char *left_char;
	const unsigned char *right_char;

	left = left != NULL && left[0] != '\0' ? left : NULL;
	right = right != NULL && right[0] != '\0' ? right : NULL;
	if (left == NULL || right == NULL) {
		return left == right;
	}
	left_char = (const unsigned char *)left;
	right_char = (const unsigned char *)right;
	while (*left_char != '\0' && *right_char != '\0') {
		if (tolower(*left_char) != tolower(*right_char)) {
			return 0;
		}
		left_char++;
		right_char++;
	}
	return *left_char == '\0' && *right_char == '\0';
}

static int sqlparser_relation_equal(
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
	return sqlparser_optional_relation_name_equal(
		       left_view.database_name, right_view.database_name) &&
	       sqlparser_optional_relation_name_equal(
		       left_view.schema_name, right_view.schema_name) &&
	       sqlparser_optional_relation_name_equal(
		       left_view.table_name, right_view.table_name) &&
	       sqlparser_optional_relation_name_equal(
		       left_view.alias_name, right_view.alias_name);
}

static PgQuery__RangeVar *sqlparser_find_duplicate_delete_from_item(
	const sqlparser_handle_t *handle,
	PgQuery__Node *node,
	const PgQuery__RangeVar *target)
{
	PgQuery__RangeVar *duplicate;

	if (node == NULL) {
		return NULL;
	}
	if (node->node_case == PG_QUERY__NODE__NODE_RANGE_VAR) {
		return node->range_var != target &&
		       sqlparser_relation_equal(handle, target, node->range_var) ?
			node->range_var : NULL;
	}
	if (node->node_case != PG_QUERY__NODE__NODE_JOIN_EXPR ||
	    node->join_expr == NULL) {
		return NULL;
	}
	duplicate = sqlparser_find_duplicate_delete_from_item(
		handle,
		node->join_expr->larg,
		target);
	return duplicate != NULL ?
		duplicate :
		sqlparser_find_duplicate_delete_from_item(
			handle,
			node->join_expr->rarg,
			target);
}

sqlparser_status_t sqlparser_find_duplicate_delete_relation(
	sqlparser_handle_t *handle,
	size_t statement_index,
	PgQuery__RangeVar *target,
	PgQuery__RangeVar **out_primary,
	PgQuery__RangeVar **out_duplicate,
	sqlparser_error_t *out_error)
{
	size_t dml_count;
	size_t dml_index;

	if (target == NULL || out_primary == NULL || out_duplicate == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"DELETE relation inputs must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_primary = target;
	*out_duplicate = NULL;
	dml_count = sqlparser_dialect_dml_result_count(
		handle,
		statement_index);
	for (dml_index = 0U; dml_index < dml_count; dml_index++) {
		sqlparser_dialect_dml_result_dml_t dml;
		ProtobufCMessage *message;
		PgQuery__DeleteStmt *stmt;
		PgQuery__RangeVar *duplicate;
		size_t index;
		sqlparser_status_t status;

		memset(&dml, 0, sizeof(dml));
		if (!sqlparser_dialect_dml_result_dml_at(
			    handle,
			    statement_index,
			    dml_index,
			    &dml)) {
			sqlparser_error_set_message(
				out_error,
				SQLPARSER_STATUS_INTERNAL_ERROR,
				"DML result metadata is inconsistent");
			return SQLPARSER_STATUS_INTERNAL_ERROR;
		}
		if (dml.kind != SQLPARSER_GRAPH_DML_DELETE ||
		    !dml.has_duplicate_target_relation) {
			continue;
		}
		message = NULL;
		status = sqlparser_get_dml_result_message(
			handle,
			statement_index,
			dml_index,
			NULL,
			&message,
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
		if (message == NULL ||
		    message->descriptor != &pg_query__delete_stmt__descriptor) {
			sqlparser_error_set_message(
				out_error,
				SQLPARSER_STATUS_INTERNAL_ERROR,
				"DELETE result metadata does not match AST");
			return SQLPARSER_STATUS_INTERNAL_ERROR;
		}
		stmt = (PgQuery__DeleteStmt *)message;
		duplicate = NULL;
		for (index = 0U; index < stmt->n_using_clause; index++) {
			duplicate = sqlparser_find_duplicate_delete_from_item(
				handle,
				stmt->using_clause[index],
				stmt->relation);
			if (duplicate != NULL) {
				break;
			}
		}
		if (stmt->relation == target) {
			if (duplicate == NULL) {
				sqlparser_error_set_message(
					out_error,
					SQLPARSER_STATUS_INTERNAL_ERROR,
					"DELETE duplicate target relation is missing");
				return SQLPARSER_STATUS_INTERNAL_ERROR;
			}
			*out_duplicate = duplicate;
			return SQLPARSER_STATUS_OK;
		}
		if (duplicate == target) {
			*out_primary = stmt->relation;
			*out_duplicate = target;
			return SQLPARSER_STATUS_OK;
		}
	}
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_handle_prepare_identifier_mutation(
	sqlparser_handle_t *handle,
	size_t statement_index,
	char **slot,
	ProtobufCMessage *location_owner,
	size_t *out_mutation_index,
	int *out_created,
	sqlparser_error_t *out_error)
{
	int32_t *location_slot;
	sqlparser_identifier_mutation_t *next;
	size_t capacity;
	size_t mutation_index;
	size_t raw_statement_index;
	size_t string_index;
	sqlparser_status_t status;

	*out_mutation_index = 0U;
	*out_created = 0;
	status = sqlparser_control_statement_ast_index(
		handle,
		statement_index,
		&raw_statement_index,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	status = sqlparser_find_raw_statement_string_index_by_slot(
		handle,
		raw_statement_index,
		slot,
		&string_index,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	for (mutation_index = 0U;
	     mutation_index < handle->identifier_mutation_count;
	     mutation_index++) {
		if (handle->identifier_mutations[mutation_index].slot == slot ||
		    (handle->identifier_mutations[mutation_index]
				     .raw_statement_index == raw_statement_index &&
		     handle->identifier_mutations[mutation_index].string_index ==
			     string_index)) {
			*out_mutation_index = mutation_index;
			return SQLPARSER_STATUS_OK;
		}
	}
	if (handle->identifier_mutation_count ==
	    handle->identifier_mutation_capacity) {
		capacity =
			handle->identifier_mutation_capacity == 0U ?
				4U :
				handle->identifier_mutation_capacity * 2U;
		if (capacity < handle->identifier_mutation_capacity ||
		    capacity >
			    SIZE_MAX / sizeof(*handle->identifier_mutations)) {
			sqlparser_error_set_message(
				out_error,
				SQLPARSER_STATUS_NO_MEMORY,
				"out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		next = (sqlparser_identifier_mutation_t *)realloc(
			handle->identifier_mutations,
			capacity * sizeof(*next));
		if (next == NULL) {
			sqlparser_error_set_message(
				out_error,
				SQLPARSER_STATUS_NO_MEMORY,
				"out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		handle->identifier_mutations = next;
		handle->identifier_mutation_capacity = capacity;
	}
	mutation_index = handle->identifier_mutation_count;
	memset(
		&handle->identifier_mutations[mutation_index],
		0,
		sizeof(handle->identifier_mutations[mutation_index]));
	handle->identifier_mutations[mutation_index].original =
		sqlparser_strdup(*slot != NULL ? *slot : "");
	if (handle->identifier_mutations[mutation_index].original == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_NO_MEMORY,
			"out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	handle->identifier_mutations[mutation_index].raw_statement_index =
		raw_statement_index;
	handle->identifier_mutations[mutation_index].string_index =
		string_index;
	handle->identifier_mutations[mutation_index].source_component_index =
		SIZE_MAX;
	handle->identifier_mutations[mutation_index].slot = slot;
	handle->identifier_mutations[mutation_index].value = *slot;
	location_slot = sqlparser_proto_location_slot(location_owner);
	handle->identifier_mutations[mutation_index].source_present =
		handle->identifier_mutations[mutation_index].original[0] != '\0' &&
		(location_slot == NULL ||
		 !sqlparser_proto_location_is_generated(*location_slot));
	handle->identifier_mutation_count++;
	*out_mutation_index = mutation_index;
	*out_created = 1;
	return SQLPARSER_STATUS_OK;
}

static void sqlparser_remove_prepared_identifier_mutations(
	sqlparser_handle_t *handle,
	const size_t *indices,
	int *remove,
	size_t count)
{
	for (;;) {
		size_t selected;
		size_t index;

		selected = count;
		for (index = 0U; index < count; index++) {
			if (remove[index] &&
			    (selected == count ||
			     indices[index] > indices[selected])) {
				selected = index;
			}
		}
		if (selected == count) {
			return;
		}
		sqlparser_handle_remove_identifier_mutation(
			handle,
			indices[selected]);
		remove[selected] = 0;
	}
}

static void sqlparser_free_relation_identifier_buffers(char **buffers)
{
	size_t index;

	for (index = 0U; index < 3U; index++) {
		free(buffers[index]);
		buffers[index] = NULL;
	}
}

static sqlparser_status_t sqlparser_replace_relation_identifier_slots(
	sqlparser_handle_t *handle,
	size_t statement_index,
	PgQuery__RangeVar *relation,
	const char *const *values,
	const char *const *spellings,
	sqlparser_error_t *out_error)
{
	char **slots[3];
	char *borrowed_spellings[3];
	char *replacements[3];
	const char *source_values[3];
	size_t borrowed_sources[3];
	size_t mutation_indices[3];
	size_t source_components[3];
	int mutation_created[3];
	int mutation_remove[3];
	int changed[3];
	int32_t *location_slot;
	int source_generated;
	size_t group;
	size_t max_group;
	size_t source_component;
	size_t index;
	sqlparser_status_t status;

	if (handle == NULL || relation == NULL || values == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"relation identifier slots are invalid");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	slots[0] = &relation->catalogname;
	slots[1] = &relation->schemaname;
	slots[2] = &relation->relname;
	memset(borrowed_spellings, 0, sizeof(borrowed_spellings));
	memset(replacements, 0, sizeof(replacements));
	for (index = 0U; index < 3U; index++) {
		const char *current;

		if (values[index] == NULL) {
			sqlparser_error_set_message(
				out_error,
				SQLPARSER_STATUS_INVALID_ARGUMENT,
				"relation identifier slots are invalid");
			return SQLPARSER_STATUS_INVALID_ARGUMENT;
		}
		current = *slots[index] != NULL ? *slots[index] : "";
		source_values[index] = current;
		borrowed_sources[index] = SIZE_MAX;
		source_components[index] = SIZE_MAX;
		changed[index] =
			strcmp(current, values[index]) != 0 ||
			(spellings != NULL && spellings[index] != NULL);
	}
	if (!changed[0] && !changed[1] && !changed[2]) {
		return SQLPARSER_STATUS_OK;
	}
	source_component = 0U;
	for (index = 0U; index < 3U; index++) {
		if (source_values[index][0] != '\0') {
			source_components[index] = source_component++;
		}
	}
	for (index = 0U; index < 3U; index++) {
		size_t source_index;

		if (!changed[index] || values[index][0] == '\0') {
			continue;
		}
		if (spellings != NULL && spellings[index] != NULL) {
			borrowed_spellings[index] =
				sqlparser_strdup(spellings[index]);
			if (borrowed_spellings[index] == NULL) {
				sqlparser_free_relation_identifier_buffers(
					borrowed_spellings);
				sqlparser_error_set_message(
					out_error,
					SQLPARSER_STATUS_NO_MEMORY,
					"out of memory");
				return SQLPARSER_STATUS_NO_MEMORY;
			}
			continue;
		}
		for (source_index = 0U; source_index < 3U;
		     source_index++) {
			if (source_index != index &&
			    source_values[source_index][0] != '\0' &&
			    values[index] == source_values[source_index]) {
				borrowed_sources[index] = source_index;
				break;
			}
		}
		if (borrowed_sources[index] != SIZE_MAX) {
			const sqlparser_identifier_mutation_t *source_mutation;
			size_t mutation_index;
			size_t source_index;

			source_index = borrowed_sources[index];
			source_mutation = NULL;
			for (mutation_index = 0U;
			     mutation_index <
				     handle->identifier_mutation_count;
			     mutation_index++) {
				if (handle->identifier_mutations[mutation_index]
					    .slot == slots[source_index]) {
					source_mutation =
						&handle->identifier_mutations
							[mutation_index];
					break;
				}
			}
			if (source_mutation != NULL &&
			    source_mutation->spelling != NULL) {
				borrowed_spellings[index] =
					sqlparser_strdup(
						source_mutation->spelling);
				if (borrowed_spellings[index] == NULL) {
					sqlparser_free_relation_identifier_buffers(
						borrowed_spellings);
					sqlparser_error_set_message(
						out_error,
						SQLPARSER_STATUS_NO_MEMORY,
						"out of memory");
					return SQLPARSER_STATUS_NO_MEMORY;
				}
				continue;
			}
			if (source_mutation != NULL &&
			    strcmp(
				    source_values[source_index],
				    source_mutation->original) != 0) {
				status =
					sqlparser_render_default_identifier_spelling(
						source_values[source_index],
						&borrowed_spellings[index],
						out_error);
				if (status != SQLPARSER_STATUS_OK) {
					sqlparser_free_relation_identifier_buffers(
						borrowed_spellings);
					return status;
				}
				continue;
			}
			if (source_mutation != NULL &&
			    source_mutation->source_present &&
			    source_mutation->has_source_component) {
				source_components[source_index] =
					source_mutation
						->source_component_index;
			}
			status =
				sqlparser_resolve_identifier_component_spelling(
					handle,
					relation->location,
					source_components[source_index],
					source_values[source_index],
					&borrowed_spellings[index],
					out_error);
			if (status != SQLPARSER_STATUS_OK) {
				sqlparser_free_relation_identifier_buffers(
					borrowed_spellings);
				return status;
			}
		}
	}
	for (index = 0U; index < 3U; index++) {
		status = sqlparser_handle_prepare_identifier_mutation(
			handle,
			statement_index,
			slots[index],
			(ProtobufCMessage *)relation,
			&mutation_indices[index],
			&mutation_created[index],
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			sqlparser_remove_prepared_identifier_mutations(
				handle,
				mutation_indices,
				mutation_created,
				index);
			sqlparser_free_relation_identifier_buffers(
				borrowed_spellings);
			return status;
		}
		mutation_remove[index] =
			strcmp(
				handle->identifier_mutations
					[mutation_indices[index]]
					.original,
				values[index]) == 0 &&
			borrowed_spellings[index] == NULL;
	}
	group = 0U;
	max_group = 0U;
	for (index = 0U;
	     index < handle->identifier_mutation_count;
	     index++) {
		if (handle->identifier_mutations[index].relation_group >
		    max_group) {
			max_group =
				handle->identifier_mutations[index].relation_group;
		}
	}
	for (index = 0U; index < 3U; index++) {
		size_t candidate;

		candidate =
			handle->identifier_mutations
				[mutation_indices[index]]
				.relation_group;
		if (candidate == 0U) {
			continue;
		}
		if (group != 0U && group != candidate) {
			sqlparser_remove_prepared_identifier_mutations(
				handle,
				mutation_indices,
				mutation_created,
				3U);
			sqlparser_free_relation_identifier_buffers(
				borrowed_spellings);
			sqlparser_error_set_message(
				out_error,
				SQLPARSER_STATUS_INTERNAL_ERROR,
				"relation identifier provenance is inconsistent");
			return SQLPARSER_STATUS_INTERNAL_ERROR;
		}
		group = candidate;
	}
	if (group == 0U) {
		if (max_group == SIZE_MAX) {
			sqlparser_remove_prepared_identifier_mutations(
				handle,
				mutation_indices,
				mutation_created,
				3U);
			sqlparser_free_relation_identifier_buffers(
				borrowed_spellings);
			sqlparser_error_set_message(
				out_error,
				SQLPARSER_STATUS_RESOURCE_LIMIT,
				"relation identifier provenance is too large");
			return SQLPARSER_STATUS_RESOURCE_LIMIT;
		}
		group = max_group + 1U;
	}
	for (index = 0U; index < 3U; index++) {
		if (!changed[index]) {
			continue;
		}
		replacements[index] = sqlparser_strdup(values[index]);
		if (replacements[index] == NULL) {
			sqlparser_free_relation_identifier_buffers(
				replacements);
			sqlparser_free_relation_identifier_buffers(
				borrowed_spellings);
			sqlparser_remove_prepared_identifier_mutations(
				handle,
				mutation_indices,
				mutation_created,
				3U);
			sqlparser_error_set_message(
				out_error,
				SQLPARSER_STATUS_NO_MEMORY,
				"out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
	}
	for (index = 0U; index < 3U; index++) {
		char *previous;

		if (!changed[index]) {
			continue;
		}
		previous = *slots[index];
		*slots[index] = replacements[index];
		replacements[index] = NULL;
		if (previous != NULL &&
		    previous != (char *)protobuf_c_empty_string) {
			free(previous);
		}
	}
	for (index = 0U; index < 3U; index++) {
		handle->identifier_mutations[mutation_indices[index]].slot =
			slots[index];
		handle->identifier_mutations[mutation_indices[index]].value =
			*slots[index];
	}
	if (mutation_remove[0] && mutation_remove[1] &&
	    mutation_remove[2]) {
		sqlparser_remove_prepared_identifier_mutations(
			handle,
			mutation_indices,
			mutation_remove,
			3U);
		sqlparser_free_relation_identifier_buffers(
			borrowed_spellings);
		return SQLPARSER_STATUS_OK;
	}

	location_slot =
		sqlparser_proto_location_slot((ProtobufCMessage *)relation);
	source_generated =
		location_slot != NULL &&
		sqlparser_proto_location_is_generated(*location_slot);
	source_component = 0U;
	for (index = 0U; index < 3U; index++) {
		sqlparser_identifier_mutation_t *mutation;

		mutation =
			&handle->identifier_mutations[mutation_indices[index]];
		mutation->relation_group = group;
		if (mutation_created[index] ||
		    mutation->source_component_index == SIZE_MAX) {
			mutation->source_present =
				!source_generated &&
				mutation->original[0] != '\0';
			mutation->has_source_component =
				mutation->source_present;
			mutation->source_component_index =
				mutation->source_present ?
					source_component++ :
					SIZE_MAX;
		} else if (mutation->source_present &&
			   mutation->has_source_component) {
			source_component =
				mutation->source_component_index + 1U >
						source_component ?
					mutation->source_component_index + 1U :
					source_component;
		}
		if (changed[index]) {
			free(mutation->spelling);
			mutation->spelling =
				borrowed_spellings[index];
			borrowed_spellings[index] = NULL;
		}
	}
	sqlparser_free_relation_identifier_buffers(borrowed_spellings);
	return SQLPARSER_STATUS_OK;
}

typedef struct {
	PgQuery__ColumnRef *column_ref;
	PgQuery__Node **old_fields;
	size_t old_field_count;
	PgQuery__Node **next_fields;
	size_t next_field_count;
	size_t next_qualifier_count;
	size_t column_source_component_index;
	char *column_spelling;
	int has_column_source_component;
	int applied;
} sqlparser_column_qualifier_plan_t;

typedef struct {
	PgQuery__ResTarget *target;
	char *old_name;
	PgQuery__Node **old_indirection;
	size_t old_indirection_count;
	char *next_name;
	PgQuery__Node **next_indirection;
	size_t next_indirection_count;
	size_t next_qualifier_count;
	size_t column_source_component_index;
	char *column_spelling;
	int has_column_source_component;
	int applied;
} sqlparser_assignment_qualifier_plan_t;

static sqlparser_identifier_mutation_t *
sqlparser_identifier_mutation_for_slot(
	sqlparser_handle_t *handle,
	char **slot)
{
	size_t index;

	if (handle == NULL || slot == NULL) {
		return NULL;
	}
	for (index = 0U; index < handle->identifier_mutation_count; index++) {
		if (handle->identifier_mutations[index].slot == slot) {
			return &handle->identifier_mutations[index];
		}
	}
	return NULL;
}

static void sqlparser_remove_identifier_mutation_for_slot(
	sqlparser_handle_t *handle,
	char **slot)
{
	size_t index;

	if (handle == NULL || slot == NULL) {
		return;
	}
	index = handle->identifier_mutation_count;
	while (index > 0U) {
		index--;
		if (handle->identifier_mutations[index].slot == slot) {
			sqlparser_handle_remove_identifier_mutation(handle, index);
		}
	}
}

static sqlparser_status_t sqlparser_current_identifier_spelling(
	sqlparser_handle_t *handle,
	int32_t location,
	size_t component_index,
	char **slot,
	char **out_spelling,
	sqlparser_error_t *out_error)
{
	sqlparser_identifier_mutation_t *mutation;

	if (slot == NULL || *slot == NULL || (*slot)[0] == '\0' ||
	    out_spelling == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INTERNAL_ERROR,
			"bound identifier is invalid");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}
	*out_spelling = NULL;
	mutation = sqlparser_identifier_mutation_for_slot(handle, slot);
	if (mutation != NULL && mutation->spelling != NULL) {
		*out_spelling = sqlparser_strdup(mutation->spelling);
		if (*out_spelling == NULL) {
			sqlparser_error_set_message(
				out_error,
				SQLPARSER_STATUS_NO_MEMORY,
				"out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		return SQLPARSER_STATUS_OK;
	}
	if (mutation != NULL && mutation->original != NULL &&
	    strcmp(*slot, mutation->original) != 0) {
		return sqlparser_render_default_identifier_spelling(
			*slot,
			out_spelling,
			out_error);
	}
	if (mutation != NULL && mutation->source_present &&
	    mutation->has_source_component) {
		component_index = mutation->source_component_index;
	}
	return sqlparser_resolve_identifier_component_spelling(
		handle,
		location,
		component_index,
		*slot,
		out_spelling,
		out_error);
}

static sqlparser_status_t sqlparser_relation_output_spelling(
	sqlparser_handle_t *handle,
	PgQuery__RangeVar *relation,
	size_t slot_index,
	const char *value,
	const char *provided_spelling,
	char **out_spelling,
	sqlparser_error_t *out_error)
{
	char **slots[3];
	sqlparser_identifier_mutation_t *mutation;
	size_t component_index;
	size_t index;

	*out_spelling = NULL;
	if (provided_spelling != NULL) {
		*out_spelling = sqlparser_strdup(provided_spelling);
		if (*out_spelling == NULL) {
			sqlparser_error_set_message(
				out_error,
				SQLPARSER_STATUS_NO_MEMORY,
				"out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		return SQLPARSER_STATUS_OK;
	}
	slots[0] = &relation->catalogname;
	slots[1] = &relation->schemaname;
	slots[2] = &relation->relname;
	mutation = sqlparser_identifier_mutation_for_slot(
		handle,
		slots[slot_index]);
	if (mutation != NULL && mutation->spelling != NULL) {
		*out_spelling = sqlparser_strdup(mutation->spelling);
		if (*out_spelling == NULL) {
			sqlparser_error_set_message(
				out_error,
				SQLPARSER_STATUS_NO_MEMORY,
				"out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		return SQLPARSER_STATUS_OK;
	}
	if (mutation != NULL && mutation->original != NULL &&
	    strcmp(value, mutation->original) != 0) {
		return sqlparser_render_default_identifier_spelling(
			value,
			out_spelling,
			out_error);
	}
	component_index = 0U;
	for (index = 0U; index < slot_index; index++) {
		if (*slots[index] != NULL && (*slots[index])[0] != '\0') {
			component_index++;
		}
	}
	if (mutation != NULL && mutation->source_present &&
	    mutation->has_source_component) {
		component_index = mutation->source_component_index;
	}
	return sqlparser_resolve_identifier_component_spelling(
		handle,
		relation->location,
		component_index,
		value,
		out_spelling,
		out_error);
}

static void sqlparser_column_qualifier_plan_clear(
	sqlparser_column_qualifier_plan_t *plan)
{
	size_t index;

	if (plan == NULL) {
		return;
	}
	if (plan->applied) {
		for (index = 0U; index + 1U < plan->old_field_count; index++) {
			sqlparser_free_proto_node(plan->old_fields[index]);
		}
		free(plan->old_fields);
	} else if (plan->next_fields != NULL) {
		for (index = 0U; index < plan->next_qualifier_count; index++) {
			sqlparser_free_proto_node(plan->next_fields[index]);
		}
		free(plan->next_fields);
	}
	free(plan->column_spelling);
	memset(plan, 0, sizeof(*plan));
}

static void sqlparser_assignment_qualifier_plan_clear(
	sqlparser_assignment_qualifier_plan_t *plan)
{
	size_t index;

	if (plan == NULL) {
		return;
	}
	if (plan->applied) {
		if (plan->old_name != NULL &&
		    plan->old_name != (char *)protobuf_c_empty_string) {
			free(plan->old_name);
		}
		for (index = 0U;
		     index + 1U < plan->old_indirection_count;
		     index++) {
			sqlparser_free_proto_node(plan->old_indirection[index]);
		}
		free(plan->old_indirection);
	} else {
		free(plan->next_name);
		if (plan->next_indirection != NULL) {
			for (index = 0U;
			     index + 1U < plan->next_indirection_count;
			     index++) {
				sqlparser_free_proto_node(
					plan->next_indirection[index]);
			}
			free(plan->next_indirection);
		}
	}
	free(plan->column_spelling);
	memset(plan, 0, sizeof(*plan));
}

static sqlparser_status_t sqlparser_prepare_column_qualifier_plan(
	sqlparser_handle_t *handle,
	PgQuery__ColumnRef *column_ref,
	const char *const *relation_parts,
	size_t relation_part_count,
	sqlparser_column_qualifier_plan_t *plan,
	sqlparser_error_t *out_error)
{
	PgQuery__Node *last;
	size_t old_qualifier_count;
	size_t part_offset;
	size_t index;

	memset(plan, 0, sizeof(*plan));
	if (column_ref == NULL || column_ref->fields == NULL ||
	    column_ref->n_fields < 2U || relation_part_count == 0U) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INTERNAL_ERROR,
			"bound column qualifier is invalid");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}
	old_qualifier_count = column_ref->n_fields - 1U;
	for (index = 0U; index < old_qualifier_count; index++) {
		if (column_ref->fields[index] == NULL ||
		    column_ref->fields[index]->node_case !=
			    PG_QUERY__NODE__NODE_STRING ||
		    column_ref->fields[index]->string == NULL) {
			sqlparser_error_set_message(
				out_error,
				SQLPARSER_STATUS_INTERNAL_ERROR,
				"bound column qualifier is invalid");
			return SQLPARSER_STATUS_INTERNAL_ERROR;
		}
	}
	last = column_ref->fields[column_ref->n_fields - 1U];
	if (last == NULL ||
	    (last->node_case != PG_QUERY__NODE__NODE_STRING &&
	     last->node_case != PG_QUERY__NODE__NODE_A_STAR)) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INTERNAL_ERROR,
			"bound column name is invalid");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}
	plan->column_ref = column_ref;
	plan->old_fields = column_ref->fields;
	plan->old_field_count = column_ref->n_fields;
	plan->next_qualifier_count =
		old_qualifier_count < relation_part_count ?
			old_qualifier_count : relation_part_count;
	plan->next_field_count = plan->next_qualifier_count + 1U;
	plan->next_fields = (PgQuery__Node **)calloc(
		plan->next_field_count,
		sizeof(*plan->next_fields));
	if (plan->next_fields == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_NO_MEMORY,
			"out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	part_offset = relation_part_count - plan->next_qualifier_count;
	for (index = 0U; index < plan->next_qualifier_count; index++) {
		plan->next_fields[index] = sqlparser_alloc_string_node(
			relation_parts[part_offset + index],
			out_error);
		if (plan->next_fields[index] == NULL) {
			return out_error != NULL &&
			       out_error->code != SQLPARSER_STATUS_OK ?
				out_error->code : SQLPARSER_STATUS_NO_MEMORY;
		}
	}
	plan->next_fields[plan->next_qualifier_count] = last;
	if (old_qualifier_count != plan->next_qualifier_count &&
	    last->node_case == PG_QUERY__NODE__NODE_STRING &&
	    last->string != NULL) {
		sqlparser_identifier_mutation_t *mutation;

		mutation = sqlparser_identifier_mutation_for_slot(
			handle,
			&last->string->sval);
		plan->column_source_component_index =
			mutation != NULL && mutation->source_present &&
				mutation->has_source_component ?
				mutation->source_component_index :
				old_qualifier_count;
		plan->has_column_source_component = 1;
		return sqlparser_current_identifier_spelling(
			handle,
			column_ref->location,
			old_qualifier_count,
			&last->string->sval,
			&plan->column_spelling,
			out_error);
	}
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_prepare_assignment_qualifier_plan(
	sqlparser_handle_t *handle,
	PgQuery__ResTarget *target,
	const char *const *relation_parts,
	size_t relation_part_count,
	sqlparser_assignment_qualifier_plan_t *plan,
	sqlparser_error_t *out_error)
{
	PgQuery__Node *last;
	size_t old_qualifier_count;
	size_t part_offset;
	size_t index;

	memset(plan, 0, sizeof(*plan));
	if (target == NULL || target->name == NULL ||
	    target->name[0] == '\0' || target->indirection == NULL ||
	    target->n_indirection == 0U || relation_part_count == 0U) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INTERNAL_ERROR,
			"bound assignment qualifier is invalid");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}
	for (index = 0U; index < target->n_indirection; index++) {
		if (target->indirection[index] == NULL ||
		    target->indirection[index]->node_case !=
			    PG_QUERY__NODE__NODE_STRING ||
		    target->indirection[index]->string == NULL) {
			sqlparser_error_set_message(
				out_error,
				SQLPARSER_STATUS_INTERNAL_ERROR,
				"bound assignment qualifier is invalid");
			return SQLPARSER_STATUS_INTERNAL_ERROR;
		}
	}
	last = target->indirection[target->n_indirection - 1U];
	old_qualifier_count = target->n_indirection;
	plan->target = target;
	plan->old_name = target->name;
	plan->old_indirection = target->indirection;
	plan->old_indirection_count = target->n_indirection;
	plan->next_qualifier_count =
		old_qualifier_count < relation_part_count ?
			old_qualifier_count : relation_part_count;
	plan->next_indirection_count = plan->next_qualifier_count;
	plan->next_indirection = (PgQuery__Node **)calloc(
		plan->next_indirection_count,
		sizeof(*plan->next_indirection));
	part_offset = relation_part_count - plan->next_qualifier_count;
	plan->next_name = sqlparser_strdup(relation_parts[part_offset]);
	if (plan->next_indirection == NULL || plan->next_name == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_NO_MEMORY,
			"out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	for (index = 1U; index < plan->next_qualifier_count; index++) {
		plan->next_indirection[index - 1U] =
			sqlparser_alloc_string_node(
				relation_parts[part_offset + index],
				out_error);
		if (plan->next_indirection[index - 1U] == NULL) {
			return out_error != NULL &&
			       out_error->code != SQLPARSER_STATUS_OK ?
				out_error->code : SQLPARSER_STATUS_NO_MEMORY;
		}
	}
	plan->next_indirection[plan->next_indirection_count - 1U] = last;
	if (old_qualifier_count != plan->next_qualifier_count) {
		sqlparser_identifier_mutation_t *mutation;

		mutation = sqlparser_identifier_mutation_for_slot(
			handle,
			&last->string->sval);
		plan->column_source_component_index =
			mutation != NULL && mutation->source_present &&
				mutation->has_source_component ?
				mutation->source_component_index :
				old_qualifier_count;
		plan->has_column_source_component = 1;
		return sqlparser_current_identifier_spelling(
			handle,
			target->location,
			old_qualifier_count,
			&last->string->sval,
			&plan->column_spelling,
			out_error);
	}
	return SQLPARSER_STATUS_OK;
}

static void sqlparser_column_qualifier_plan_apply(
	sqlparser_handle_t *handle,
	sqlparser_column_qualifier_plan_t *plan)
{
	size_t index;

	for (index = 0U; index + 1U < plan->old_field_count; index++) {
		PgQuery__Node *node;

		node = plan->old_fields[index];
		if (node != NULL && node->node_case == PG_QUERY__NODE__NODE_STRING &&
		    node->string != NULL) {
			sqlparser_remove_identifier_mutation_for_slot(
				handle,
				&node->string->sval);
		}
	}
	plan->column_ref->fields = plan->next_fields;
	plan->column_ref->n_fields = plan->next_field_count;
	plan->next_fields = NULL;
	plan->applied = 1;
}

static void sqlparser_assignment_qualifier_plan_apply(
	sqlparser_handle_t *handle,
	sqlparser_assignment_qualifier_plan_t *plan)
{
	size_t index;

	sqlparser_remove_identifier_mutation_for_slot(
		handle,
		&plan->target->name);
	for (index = 0U;
	     index + 1U < plan->old_indirection_count;
	     index++) {
		PgQuery__Node *node;

		node = plan->old_indirection[index];
		if (node != NULL && node->node_case == PG_QUERY__NODE__NODE_STRING &&
		    node->string != NULL) {
			sqlparser_remove_identifier_mutation_for_slot(
				handle,
				&node->string->sval);
		}
	}
	plan->target->name = plan->next_name;
	plan->target->indirection = plan->next_indirection;
	plan->target->n_indirection = plan->next_indirection_count;
	plan->next_name = NULL;
	plan->next_indirection = NULL;
	plan->applied = 1;
}

static sqlparser_status_t sqlparser_install_identifier_spelling(
	sqlparser_handle_t *handle,
	size_t statement_index,
	char **slot,
	ProtobufCMessage *owner,
	char **owned_spelling,
	int generated_slot,
	int has_source_component,
	size_t source_component_index,
	sqlparser_error_t *out_error)
{
	sqlparser_identifier_mutation_t *mutation;
	size_t mutation_index;
	int mutation_created;
	sqlparser_status_t status;

	if (owned_spelling == NULL || *owned_spelling == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INTERNAL_ERROR,
			"identifier spelling is missing");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}
	status = sqlparser_handle_prepare_identifier_mutation(
		handle,
		statement_index,
		slot,
		owner,
		&mutation_index,
		&mutation_created,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	(void)mutation_created;
	mutation = &handle->identifier_mutations[mutation_index];
	free(mutation->spelling);
	mutation->spelling = *owned_spelling;
	*owned_spelling = NULL;
	mutation->slot = slot;
	mutation->value = *slot;
	if (generated_slot) {
		mutation->source_present = 0;
		mutation->has_source_component = 0;
		mutation->source_component_index = SIZE_MAX;
	} else if (has_source_component && mutation->source_present) {
		mutation->has_source_component = 1;
		mutation->source_component_index = source_component_index;
	}
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_install_bound_qualifier_spellings(
	sqlparser_handle_t *handle,
	size_t statement_index,
	const char *const *relation_spellings,
	size_t relation_part_count,
	sqlparser_column_qualifier_plan_t *column_plans,
	size_t column_plan_count,
	sqlparser_assignment_qualifier_plan_t *assignment_plans,
	size_t assignment_plan_count,
	sqlparser_error_t *out_error)
{
	char *spelling;
	size_t part_offset;
	size_t plan_index;
	size_t index;
	sqlparser_status_t status;

	status = sqlparser_handle_rebind_identifier_mutations(
		handle,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	for (plan_index = 0U; plan_index < column_plan_count; plan_index++) {
		sqlparser_column_qualifier_plan_t *plan;

		plan = &column_plans[plan_index];
		part_offset = relation_part_count - plan->next_qualifier_count;
		for (index = 0U; index < plan->next_qualifier_count; index++) {
			PgQuery__Node *node;

			node = plan->column_ref->fields[index];
			spelling = sqlparser_strdup(
				relation_spellings[part_offset + index]);
			if (spelling == NULL) {
				sqlparser_error_set_message(
					out_error,
					SQLPARSER_STATUS_NO_MEMORY,
					"out of memory");
				return SQLPARSER_STATUS_NO_MEMORY;
			}
			status = sqlparser_install_identifier_spelling(
				handle,
				statement_index,
				&node->string->sval,
				(ProtobufCMessage *)node->string,
				&spelling,
				1,
				0,
				0U,
				out_error);
			free(spelling);
			if (status != SQLPARSER_STATUS_OK) {
				return status;
			}
		}
		if (plan->column_spelling != NULL) {
			PgQuery__Node *node;

			node = plan->column_ref->fields[
				plan->column_ref->n_fields - 1U];
			status = sqlparser_install_identifier_spelling(
				handle,
				statement_index,
				&node->string->sval,
				(ProtobufCMessage *)node->string,
				&plan->column_spelling,
				0,
				plan->has_column_source_component,
				plan->column_source_component_index,
				out_error);
			if (status != SQLPARSER_STATUS_OK) {
				return status;
			}
		}
	}
	for (plan_index = 0U;
	     plan_index < assignment_plan_count;
	     plan_index++) {
		sqlparser_assignment_qualifier_plan_t *plan;

		plan = &assignment_plans[plan_index];
		part_offset = relation_part_count - plan->next_qualifier_count;
		spelling = sqlparser_strdup(relation_spellings[part_offset]);
		if (spelling == NULL) {
			sqlparser_error_set_message(
				out_error,
				SQLPARSER_STATUS_NO_MEMORY,
				"out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		status = sqlparser_install_identifier_spelling(
			handle,
			statement_index,
			&plan->target->name,
			(ProtobufCMessage *)plan->target,
			&spelling,
			1,
			0,
			0U,
			out_error);
		free(spelling);
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
		for (index = 1U; index < plan->next_qualifier_count; index++) {
			PgQuery__Node *node;

			node = plan->target->indirection[index - 1U];
			spelling = sqlparser_strdup(
				relation_spellings[part_offset + index]);
			if (spelling == NULL) {
				sqlparser_error_set_message(
					out_error,
					SQLPARSER_STATUS_NO_MEMORY,
					"out of memory");
				return SQLPARSER_STATUS_NO_MEMORY;
			}
			status = sqlparser_install_identifier_spelling(
				handle,
				statement_index,
				&node->string->sval,
				(ProtobufCMessage *)node->string,
				&spelling,
				1,
				0,
				0U,
				out_error);
			free(spelling);
			if (status != SQLPARSER_STATUS_OK) {
				return status;
			}
		}
		if (plan->column_spelling != NULL) {
			PgQuery__Node *node;

			node = plan->target->indirection[
				plan->target->n_indirection - 1U];
			status = sqlparser_install_identifier_spelling(
				handle,
				statement_index,
				&node->string->sval,
				(ProtobufCMessage *)node->string,
				&plan->column_spelling,
				0,
				plan->has_column_source_component,
				plan->column_source_component_index,
				out_error);
			if (status != SQLPARSER_STATUS_OK) {
				return status;
			}
		}
	}
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_replace_relation_and_bound_qualifiers(
	sqlparser_handle_t *handle,
	size_t statement_index,
	PgQuery__RangeVar *relation,
	PgQuery__RangeVar *duplicate_relation,
	const sqlparser_relation_bindings_t *bindings,
	const char *const *values,
	const char *const *spellings,
	void *dialect_state,
	sqlparser_error_t *out_error)
{
	sqlparser_column_qualifier_plan_t *column_plans;
	sqlparser_assignment_qualifier_plan_t *assignment_plans;
	char *owned_relation_spellings[3];
	char *owned_values[3];
	const char *relation_parts[3];
	const char *relation_spellings[3];
	const char *stable_values[3];
	size_t relation_part_count;
	size_t column_plan_count;
	size_t assignment_plan_count;
	size_t qualified_assignment_count;
	size_t index;
	sqlparser_status_t status;
	int commit_attempted;
	int needs_stable_values;

	column_plans = NULL;
	assignment_plans = NULL;
	memset(owned_relation_spellings, 0, sizeof(owned_relation_spellings));
	memset(owned_values, 0, sizeof(owned_values));
	column_plan_count = 0U;
	assignment_plan_count = 0U;
	qualified_assignment_count = 0U;
	relation_part_count = 0U;
	commit_attempted = 0;
	needs_stable_values = 0;
	if (values == NULL || bindings == NULL ||
	    (bindings->column_ref_count > 0U &&
	     bindings->column_refs == NULL) ||
	    (bindings->assignment_target_count > 0U &&
	     bindings->assignment_targets == NULL)) {
		status = SQLPARSER_STATUS_INVALID_ARGUMENT;
		sqlparser_error_set_message(
			out_error,
			status,
			"relation replacement inputs are invalid");
		goto cleanup;
	}
	for (index = 0U;
	     index < bindings->assignment_target_count;
	     index++) {
		if (bindings->assignment_targets[index] == NULL) {
			status = SQLPARSER_STATUS_INTERNAL_ERROR;
			sqlparser_error_set_message(
				out_error,
				status,
				"bound assignment target is missing");
			goto cleanup;
		}
		if (bindings->assignment_targets[index]->n_indirection > 0U) {
			qualified_assignment_count++;
		}
	}
	needs_stable_values =
		duplicate_relation != NULL || bindings->column_ref_count > 0U ||
		qualified_assignment_count > 0U;
	for (index = 0U; index < 3U; index++) {
		stable_values[index] = values[index];
		if (!needs_stable_values || values[index] == NULL) {
			continue;
		}
		owned_values[index] = sqlparser_strdup(values[index]);
		if (owned_values[index] == NULL) {
			status = SQLPARSER_STATUS_NO_MEMORY;
			sqlparser_error_set_message(
				out_error,
				status,
				"out of memory");
			goto cleanup;
		}
		stable_values[index] = owned_values[index];
	}
	status = sqlparser_replace_relation_identifier_slots(
		handle,
		statement_index,
		relation,
		values,
		spellings,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		goto cleanup;
	}
	if (duplicate_relation != NULL) {
		status = sqlparser_replace_relation_identifier_slots(
			handle,
			statement_index,
			duplicate_relation,
			stable_values,
			spellings,
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			goto cleanup;
		}
	}
	if (bindings->column_ref_count > 0U ||
	    qualified_assignment_count > 0U) {
		for (index = 0U; index < 3U; index++) {
			if (stable_values[index] == NULL ||
			    stable_values[index][0] == '\0') {
				continue;
			}
			status = sqlparser_relation_output_spelling(
				handle,
				relation,
				index,
				stable_values[index],
				spellings != NULL ? spellings[index] : NULL,
				&owned_relation_spellings[index],
				out_error);
			if (status != SQLPARSER_STATUS_OK) {
				goto cleanup;
			}
			relation_parts[relation_part_count] =
				stable_values[index];
			relation_spellings[relation_part_count] =
				owned_relation_spellings[index];
			relation_part_count++;
		}
		if (relation_part_count == 0U) {
			status = SQLPARSER_STATUS_INTERNAL_ERROR;
			sqlparser_error_set_message(
				out_error,
				status,
				"relation identifier path is empty");
			goto cleanup;
		}
	}
	if (bindings->column_ref_count > 0U) {
		column_plans = (sqlparser_column_qualifier_plan_t *)calloc(
			bindings->column_ref_count,
			sizeof(*column_plans));
		if (column_plans == NULL) {
			status = SQLPARSER_STATUS_NO_MEMORY;
			sqlparser_error_set_message(
				out_error,
				status,
				"out of memory");
			goto cleanup;
		}
		for (index = 0U; index < bindings->column_ref_count; index++) {
			column_plan_count++;
			status = sqlparser_prepare_column_qualifier_plan(
				handle,
				bindings->column_refs[index],
				relation_parts,
				relation_part_count,
				&column_plans[index],
				out_error);
			if (status != SQLPARSER_STATUS_OK) {
				goto cleanup;
			}
		}
	}
	if (bindings->assignment_target_count > 0U) {
		if (qualified_assignment_count > 0U) {
			assignment_plans =
				(sqlparser_assignment_qualifier_plan_t *)calloc(
					qualified_assignment_count,
					sizeof(*assignment_plans));
			if (assignment_plans == NULL) {
				status = SQLPARSER_STATUS_NO_MEMORY;
				sqlparser_error_set_message(
					out_error,
					status,
					"out of memory");
				goto cleanup;
			}
		}
		for (index = 0U;
		     index < bindings->assignment_target_count;
		     index++) {
			if (bindings->assignment_targets[index]->n_indirection ==
			    0U) {
				continue;
			}
			assignment_plan_count++;
			status = sqlparser_prepare_assignment_qualifier_plan(
				handle,
				bindings->assignment_targets[index],
				relation_parts,
				relation_part_count,
				&assignment_plans[assignment_plan_count - 1U],
				out_error);
			if (status != SQLPARSER_STATUS_OK) {
				goto cleanup;
			}
		}
	}
	for (index = 0U; index < column_plan_count; index++) {
		sqlparser_column_qualifier_plan_apply(handle, &column_plans[index]);
	}
	for (index = 0U; index < assignment_plan_count; index++) {
		sqlparser_assignment_qualifier_plan_apply(
			handle,
			&assignment_plans[index]);
	}
	status = sqlparser_install_bound_qualifier_spellings(
		handle,
		statement_index,
		relation_spellings,
		relation_part_count,
		column_plans,
		column_plan_count,
		assignment_plans,
		assignment_plan_count,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		goto cleanup;
	}
	commit_attempted = 1;
	status = sqlparser_handle_commit_ast_with_dialect_state(
		handle,
		dialect_state,
		out_error);
	dialect_state = NULL;

cleanup:
	if (!commit_attempted && dialect_state != NULL) {
		sqlparser_handle_discard_dialect_state(handle, dialect_state);
	}
	for (index = 0U; index < column_plan_count; index++) {
		sqlparser_column_qualifier_plan_clear(&column_plans[index]);
	}
	for (index = 0U; index < assignment_plan_count; index++) {
		sqlparser_assignment_qualifier_plan_clear(
			&assignment_plans[index]);
	}
	free(column_plans);
	free(assignment_plans);
	sqlparser_free_relation_identifier_buffers(owned_relation_spellings);
	sqlparser_free_relation_identifier_buffers(owned_values);
	return status;
}

sqlparser_status_t sqlparser_statement_kind(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	sqlparser_statement_kind_t *out_kind,
	sqlparser_error_t *out_error)
{
	PgQuery__Node *statement;
	sqlparser_status_t status;
	sqlparser_handle_t *mutable_handle;

	if (out_kind == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"out_kind must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	*out_kind = SQLPARSER_STATEMENT_KIND_UNKNOWN;
	sqlparser_error_clear(out_error);
	if (sqlparser_control_unit_is_condition(handle, statement_index)) {
		*out_kind = SQLPARSER_STATEMENT_KIND_CONDITION;
		return SQLPARSER_STATUS_OK;
	}
	mutable_handle = (sqlparser_handle_t *)handle;
	status = sqlparser_get_statement_node(mutable_handle, statement_index, &statement, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	*out_kind = sqlparser_statement_kind_from_case(statement->node_case);
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_statement_node_name(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	const char **out_name,
	sqlparser_error_t *out_error)
{
	PgQuery__Node *statement;
	sqlparser_status_t status;
	sqlparser_handle_t *mutable_handle;

	if (out_name == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"out_name must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	*out_name = NULL;
	sqlparser_error_clear(out_error);
	if (sqlparser_control_unit_is_condition(handle, statement_index)) {
		*out_name = "ConditionExpr";
		return SQLPARSER_STATUS_OK;
	}
	mutable_handle = (sqlparser_handle_t *)handle;
	status = sqlparser_get_statement_node(mutable_handle, statement_index, &statement, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	*out_name = sqlparser_statement_node_name_from_case(statement->node_case);
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_statement_target_relation(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	sqlparser_relation_view_t *out_relation,
	sqlparser_error_t *out_error)
{
	PgQuery__Node *statement;
	PgQuery__RangeVar *relation;
	sqlparser_status_t status;
	sqlparser_handle_t *mutable_handle;

	if (out_relation == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"out_relation must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	sqlparser_relation_view_clear(out_relation);
	sqlparser_error_clear(out_error);
	mutable_handle = (sqlparser_handle_t *)handle;
	status = sqlparser_get_statement_node(mutable_handle, statement_index, &statement, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	relation = NULL;
	switch (statement->node_case) {
		case PG_QUERY__NODE__NODE_INSERT_STMT:
			if (statement->insert_stmt != NULL) {
				relation = statement->insert_stmt->relation;
			}
			break;
		case PG_QUERY__NODE__NODE_UPDATE_STMT:
			if (statement->update_stmt != NULL) {
				relation = statement->update_stmt->relation;
			}
			break;
		case PG_QUERY__NODE__NODE_DELETE_STMT:
			if (statement->delete_stmt != NULL) {
				relation = statement->delete_stmt->relation;
			}
			break;
		case PG_QUERY__NODE__NODE_MERGE_STMT:
			if (statement->merge_stmt != NULL) {
				relation = statement->merge_stmt->relation;
			}
			break;
		default:
			sqlparser_error_set_message(
				out_error,
				SQLPARSER_STATUS_UNSUPPORTED,
				"statement does not expose a single target relation");
			return SQLPARSER_STATUS_UNSUPPORTED;
	}

	if (relation == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INTERNAL_ERROR,
			"target relation is missing");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}

	sqlparser_fill_relation_view_for_handle(handle, relation, out_relation);
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_statement_relation_count(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	size_t *out_count,
	sqlparser_error_t *out_error)
{
	sqlparser_handle_t *mutable_handle;

	if (out_count == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"out_count must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	*out_count = 0U;
	sqlparser_error_clear(out_error);
	mutable_handle = (sqlparser_handle_t *)handle;
	return sqlparser_search_statement_messages(
		mutable_handle,
		statement_index,
		&pg_query__range_var__descriptor,
		NULL,
		0,
		0U,
		out_count,
		NULL,
		out_error);
}

sqlparser_status_t sqlparser_statement_relation(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	size_t relation_index,
	sqlparser_relation_view_t *out_relation,
	sqlparser_error_t *out_error)
{
	sqlparser_handle_t *mutable_handle;
	ProtobufCMessage *message;
	sqlparser_status_t status;

	if (out_relation == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"out_relation must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	sqlparser_relation_view_clear(out_relation);
	sqlparser_error_clear(out_error);
	mutable_handle = (sqlparser_handle_t *)handle;
	message = NULL;
	status = sqlparser_search_statement_messages(
		mutable_handle,
		statement_index,
		&pg_query__range_var__descriptor,
		NULL,
		1,
		relation_index,
		NULL,
		&message,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	if (message == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"relation_index is out of range");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	sqlparser_fill_relation_view_for_handle(handle, (PgQuery__RangeVar *)message, out_relation);
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_statement_set_relation_name_in_place(
	sqlparser_handle_t *handle,
	size_t statement_index,
	size_t relation_index,
	const char *schema_name,
	const char *table_name,
	size_t source_encoding,
	sqlparser_error_t *out_error)
{
	PgQuery__RangeVar *relation;
	PgQuery__RangeVar *selected_relation;
	PgQuery__RangeVar *duplicate_relation;
	ProtobufCMessage *message;
	sqlparser_relation_bindings_t bindings;
	const char *current_values[3];
	const char *values[3];
	size_t schema_source;
	size_t table_source;
	sqlparser_status_t status;

	if (table_name == NULL || table_name[0] == '\0') {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"table_name must not be NULL or empty");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	if ((source_encoding & ~(size_t)0x0fU) != 0U) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"relation source encoding is invalid");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	schema_source = source_encoding & 0x03U;
	table_source = (source_encoding >> 2U) & 0x03U;

	sqlparser_error_clear(out_error);
	message = NULL;
	status = sqlparser_search_statement_messages(
		handle,
		statement_index,
		&pg_query__range_var__descriptor,
		NULL,
		1,
		relation_index,
		NULL,
		&message,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	if (message == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"relation_index is out of range");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	selected_relation = (PgQuery__RangeVar *)message;
	if (strcmp(
		    selected_relation->schemaname != NULL ?
			    selected_relation->schemaname : "",
		    schema_name != NULL ? schema_name : "") == 0 &&
	    strcmp(
		    selected_relation->relname != NULL ?
			    selected_relation->relname : "",
		    table_name) == 0) {
		return SQLPARSER_STATUS_OK;
	}
	status = sqlparser_find_duplicate_delete_relation(
		handle,
		statement_index,
		selected_relation,
		&relation,
		&duplicate_relation,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	memset(&bindings, 0, sizeof(bindings));
	status = sqlparser_collect_relation_bindings(
		handle,
		statement_index,
		relation,
		&bindings,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_relation_bindings_clear(&bindings);
		return status;
	}
	current_values[0] =
		relation->catalogname != NULL ? relation->catalogname : "";
	current_values[1] =
		relation->schemaname != NULL ? relation->schemaname : "";
	current_values[2] =
		relation->relname != NULL ? relation->relname : "";
	values[0] = current_values[0];
	values[1] = schema_source > 0U ?
		current_values[schema_source - 1U] :
		(schema_name != NULL ? schema_name : "");
	values[2] = table_source > 0U ?
		current_values[table_source - 1U] :
		table_name;
	status = sqlparser_replace_relation_and_bound_qualifiers(
		handle,
		statement_index,
		relation,
		duplicate_relation,
		&bindings,
		values,
		NULL,
		NULL,
		out_error);
	sqlparser_relation_bindings_clear(&bindings);
	return status;
}

sqlparser_status_t sqlparser_statement_set_relation_name(
	sqlparser_handle_t *handle,
	size_t statement_index,
	size_t relation_index,
	const char *schema_name,
	const char *table_name,
	sqlparser_error_t *out_error)
{
	sqlparser_patch_t patch;
	sqlparser_selector_t selector;
	sqlparser_status_t status;

	memset(&patch, 0, sizeof(patch));
	status = sqlparser_statement_relation_patch_source_encoding(
		handle,
		statement_index,
		relation_index,
		schema_name,
		table_name,
		&patch.index,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	memset(&selector, 0, sizeof(selector));
	selector.kind = SQLPARSER_SELECTOR_KIND_RELATION;
	selector.statement_index = statement_index;
	selector.item_index = relation_index;
	patch.op = SQLPARSER_PATCH_REPLACE;
	patch.name = table_name;
	patch.default_sql = schema_name;
	return sqlparser_selector_apply_single_patch(
		handle,
		&selector,
		&patch,
		out_error);
}

sqlparser_status_t sqlparser_statement_relation_patch_source_encoding(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	size_t relation_index,
	const char *schema_name,
	const char *table_name,
	size_t *out_source_encoding,
	sqlparser_error_t *out_error)
{
	const char *relation_parts[3];
	sqlparser_relation_view_t relation;
	size_t schema_source;
	size_t source_index;
	size_t table_source;
	sqlparser_status_t status;

	if (table_name == NULL || table_name[0] == '\0') {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"table_name must not be NULL or empty");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	if (out_source_encoding == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"source encoding output must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_source_encoding = 0U;
	memset(&relation, 0, sizeof(relation));
	status = sqlparser_statement_relation(
		handle,
		statement_index,
		relation_index,
		&relation,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	relation_parts[0] =
		relation.database_name != NULL ? relation.database_name : "";
	relation_parts[1] =
		relation.schema_name != NULL ? relation.schema_name : "";
	relation_parts[2] =
		relation.table_name != NULL ? relation.table_name : "";
	schema_source = 0U;
	table_source = 0U;
	for (source_index = 0U; source_index < 3U; source_index++) {
		if (schema_name == relation_parts[source_index]) {
			schema_source = source_index + 1U;
		}
		if (table_name == relation_parts[source_index]) {
			table_source = source_index + 1U;
		}
	}
	*out_source_encoding = schema_source | (table_source << 2U);
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_statement_name_count(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	size_t *out_count,
	sqlparser_error_t *out_error)
{
	PgQuery__Node *statement;
	sqlparser_name_search_t search;
	sqlparser_status_t status;
	sqlparser_handle_t *mutable_handle;

	if (out_count == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"out_count must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	*out_count = 0U;
	sqlparser_error_clear(out_error);
	mutable_handle = (sqlparser_handle_t *)handle;
	status = sqlparser_get_statement_node(mutable_handle, statement_index, &statement, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	memset(&search, 0, sizeof(search));
	status = sqlparser_walk_message_names((ProtobufCMessage *)statement, NULL, &search);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	*out_count = search.seen;
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_statement_name(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	size_t name_index,
	sqlparser_name_view_t *out_name,
	sqlparser_error_t *out_error)
{
	PgQuery__Node *statement;
	sqlparser_name_search_t search;
	sqlparser_status_t status;
	sqlparser_handle_t *mutable_handle;

	if (out_name == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"out_name must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	sqlparser_name_view_clear(out_name);
	sqlparser_error_clear(out_error);
	mutable_handle = (sqlparser_handle_t *)handle;
	status = sqlparser_get_statement_node(mutable_handle, statement_index, &statement, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	memset(&search, 0, sizeof(search));
	search.want_target = 1;
	search.target_index = name_index;
	search.name_view = out_name;
	status = sqlparser_walk_message_names((ProtobufCMessage *)statement, NULL, &search);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	if (search.target_slot == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"name_index is out of range");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_statement_set_name_spelling(
	sqlparser_handle_t *handle,
	size_t statement_index,
	size_t name_index,
	const char *value,
	const char *spelling,
	sqlparser_error_t *out_error)
{
	PgQuery__Node *statement;
	sqlparser_name_search_t search;
	char *next_spelling;
	char *previous_spelling;
	const char *generated_spelling;
	int32_t *location_slot;
	int32_t previous_location;
	size_t relation_group;
	size_t generated_spelling_length;
	size_t mutation_index;
	int location_retagged;
	int value_changed;
	int mutation_created;
	int remove_mutation;
	sqlparser_status_t status;

	if (value == NULL || value[0] == '\0') {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"value must not be NULL or empty");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	if (spelling != NULL && spelling[0] == '\0') {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"identifier spelling must not be empty");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	sqlparser_error_clear(out_error);
	status = sqlparser_get_statement_node(handle, statement_index, &statement, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	memset(&search, 0, sizeof(search));
	search.want_target = 1;
	search.target_index = name_index;
	status = sqlparser_walk_message_names((ProtobufCMessage *)statement, NULL, &search);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	if (search.target_slot == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"name_index is out of range");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	value_changed = strcmp(*search.target_slot, value) != 0;
	if (!value_changed && spelling == NULL) {
		return SQLPARSER_STATUS_OK;
	}

	next_spelling =
		spelling != NULL ? sqlparser_strdup(spelling) : NULL;
	if (spelling != NULL && next_spelling == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_NO_MEMORY,
			"out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	status = sqlparser_handle_prepare_identifier_mutation(
		handle,
		statement_index,
		search.target_slot,
		search.target_owner,
		&mutation_index,
		&mutation_created,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		free(next_spelling);
		return status;
	}
	location_slot = sqlparser_proto_location_slot(search.target_owner);
	previous_location =
		location_slot != NULL ? *location_slot : 0;
	location_retagged = 0;
	generated_spelling = NULL;
	generated_spelling_length = 0U;
	if (value_changed &&
	    spelling == NULL &&
	    !handle->identifier_mutations[mutation_index].source_present &&
	    search.target_owner != NULL &&
	    search.target_owner->descriptor == &pg_query__string__descriptor &&
	    location_slot != NULL &&
	    sqlparser_proto_location_is_identifier_spelling(*location_slot) &&
	    sqlparser_handle_identifier_spelling(
		    handle,
		    *location_slot,
		    0U,
		    &generated_spelling,
		    &generated_spelling_length) &&
	    generated_spelling_length > 0U &&
	    !(generated_spelling_length > 2U &&
	      (generated_spelling[0] == 'U' ||
	       generated_spelling[0] == 'u') &&
	      generated_spelling[1] == '&' &&
	      generated_spelling[2] == '"')) {
		sqlparser_proto_identifier_style_t style;

		if (generated_spelling[0] == '`') {
			style =
				SQLPARSER_PROTO_IDENTIFIER_STYLE_BACKTICK_QUOTED;
		} else if (generated_spelling[0] == '[') {
			style =
				SQLPARSER_PROTO_IDENTIFIER_STYLE_BRACKET_QUOTED;
		} else if (generated_spelling[0] == '"') {
			style =
				SQLPARSER_PROTO_IDENTIFIER_STYLE_DOUBLE_QUOTED;
		} else {
			style = SQLPARSER_PROTO_IDENTIFIER_STYLE_UNQUOTED;
		}
		*location_slot =
			SQLPARSER_PROTO_LOCATION_GENERATED_STYLE_BASE -
			(int32_t)style;
		location_retagged = 1;
	}
	if (!value_changed &&
	    ((handle->identifier_mutations[mutation_index].spelling == NULL &&
	      next_spelling == NULL) ||
	     (handle->identifier_mutations[mutation_index].spelling != NULL &&
	      next_spelling != NULL &&
	      strcmp(
		      handle->identifier_mutations[mutation_index].spelling,
		      next_spelling) == 0))) {
		free(next_spelling);
		if (mutation_created) {
			sqlparser_handle_remove_identifier_mutation(
				handle,
				mutation_index);
		}
		return SQLPARSER_STATUS_OK;
	}
	if (value_changed) {
		status = sqlparser_replace_proto_string(
			search.target_slot,
			value,
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			if (location_retagged) {
				*location_slot = previous_location;
			}
			free(next_spelling);
			if (mutation_created) {
				sqlparser_handle_remove_identifier_mutation(
					handle,
					mutation_index);
			}
			return status;
		}
	}
	handle->identifier_mutations[mutation_index].slot =
		search.target_slot;
	handle->identifier_mutations[mutation_index].value =
		*search.target_slot;
	previous_spelling =
		handle->identifier_mutations[mutation_index].spelling;
	handle->identifier_mutations[mutation_index].spelling =
		next_spelling;
	relation_group =
		handle->identifier_mutations[mutation_index].relation_group;
	remove_mutation =
		spelling == NULL &&
		relation_group == 0U &&
		strcmp(
			handle->identifier_mutations[mutation_index].original,
			value) == 0;

	status = sqlparser_handle_commit_ast(handle, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		handle->identifier_mutations[mutation_index].spelling =
			previous_spelling;
		free(next_spelling);
		if (mutation_created) {
			sqlparser_handle_remove_identifier_mutation(
				handle,
				mutation_index);
		}
		return status;
	}
	free(previous_spelling);
	if (remove_mutation) {
		sqlparser_handle_remove_identifier_mutation(
			handle,
			mutation_index);
	} else if (relation_group != 0U) {
		int restored;
		size_t index;

		restored = 1;
		for (index = 0U;
		     index < handle->identifier_mutation_count;
		     index++) {
			sqlparser_identifier_mutation_t *mutation;

			mutation = &handle->identifier_mutations[index];
			if (mutation->relation_group == relation_group &&
			    (mutation->value == NULL ||
			     strcmp(
				     mutation->value,
				     mutation->original) != 0)) {
				restored = 0;
				break;
			}
		}
		if (restored) {
			index = handle->identifier_mutation_count;
			while (index > 0U) {
				index--;
				if (handle->identifier_mutations[index]
					    .relation_group ==
				    relation_group) {
					sqlparser_handle_remove_identifier_mutation(
						handle,
						index);
				}
			}
		}
	}
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_statement_set_name(
	sqlparser_handle_t *handle,
	size_t statement_index,
	size_t name_index,
	const char *value,
	sqlparser_error_t *out_error)
{
	sqlparser_patch_t patch;
	sqlparser_selector_t selector;

	memset(&selector, 0, sizeof(selector));
	selector.kind = SQLPARSER_SELECTOR_KIND_NAME;
	selector.statement_index = statement_index;
	selector.item_index = name_index;
	memset(&patch, 0, sizeof(patch));
	patch.op = SQLPARSER_PATCH_REPLACE;
	patch.default_sql = value;
	return sqlparser_selector_apply_single_patch(
		handle,
		&selector,
		&patch,
		out_error);
}
