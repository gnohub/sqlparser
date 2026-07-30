#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "sqlparser_ast_internal.h"

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
		 *location_slot != SQLPARSER_PROTO_LOCATION_GENERATED);
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

sqlparser_status_t sqlparser_replace_relation_identifier_slots(
	sqlparser_handle_t *handle,
	size_t statement_index,
	PgQuery__RangeVar *relation,
	const char *const *values,
	sqlparser_error_t *out_error)
{
	char **slots[3];
	size_t mutation_indices[3];
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
		changed[index] = strcmp(current, values[index]) != 0;
	}
	if (!changed[0] && !changed[1] && !changed[2]) {
		return SQLPARSER_STATUS_OK;
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
			return status;
		}
		mutation_remove[index] =
			strcmp(
				handle->identifier_mutations
					[mutation_indices[index]]
					.original,
				values[index]) == 0;
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
		status = sqlparser_replace_proto_string(
			slots[index],
			values[index],
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			sqlparser_handle_clear_ast(handle);
			sqlparser_remove_prepared_identifier_mutations(
				handle,
				mutation_indices,
				mutation_created,
				3U);
			return status;
		}
	}
	for (index = 0U; index < 3U; index++) {
		handle->identifier_mutations[mutation_indices[index]].slot =
			slots[index];
		handle->identifier_mutations[mutation_indices[index]].value =
			*slots[index];
	}
	status = sqlparser_handle_commit_ast(handle, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_remove_prepared_identifier_mutations(
			handle,
			mutation_indices,
			mutation_created,
			3U);
		return status;
	}
	if (mutation_remove[0] && mutation_remove[1] &&
	    mutation_remove[2]) {
		sqlparser_remove_prepared_identifier_mutations(
			handle,
			mutation_indices,
			mutation_remove,
			3U);
		return SQLPARSER_STATUS_OK;
	}

	location_slot =
		sqlparser_proto_location_slot((ProtobufCMessage *)relation);
	source_generated =
		location_slot != NULL &&
		*location_slot == SQLPARSER_PROTO_LOCATION_GENERATED;
	source_component = 0U;
	for (index = 0U; index < 3U; index++) {
		sqlparser_identifier_mutation_t *mutation;

		mutation =
			&handle->identifier_mutations[mutation_indices[index]];
		mutation->relation_group = group;
		mutation->source_present =
			!source_generated && mutation->original[0] != '\0';
		mutation->has_source_component =
			mutation->source_present;
		mutation->source_component_index =
			mutation->source_present ?
				source_component++ :
				SIZE_MAX;
	}
	return SQLPARSER_STATUS_OK;
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

sqlparser_status_t sqlparser_statement_set_relation_name(
	sqlparser_handle_t *handle,
	size_t statement_index,
	size_t relation_index,
	const char *schema_name,
	const char *table_name,
	sqlparser_error_t *out_error)
{
	PgQuery__RangeVar *relation;
	ProtobufCMessage *message;
	const char *next_schema_name;
	const char *values[3];
	sqlparser_status_t status;

	if (table_name == NULL || table_name[0] == '\0') {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"table_name must not be NULL or empty");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

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

	relation = (PgQuery__RangeVar *)message;
	next_schema_name = schema_name != NULL ? schema_name : "";
	values[0] =
		relation->catalogname != NULL ? relation->catalogname : "";
	values[1] = next_schema_name;
	values[2] = table_name;
	return sqlparser_replace_relation_identifier_slots(
		handle,
		statement_index,
		relation,
		values,
		out_error);
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

sqlparser_status_t sqlparser_statement_set_name(
	sqlparser_handle_t *handle,
	size_t statement_index,
	size_t name_index,
	const char *value,
	sqlparser_error_t *out_error)
{
	PgQuery__Node *statement;
	sqlparser_name_search_t search;
	size_t relation_group;
	size_t mutation_index;
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
	if (strcmp(*search.target_slot, value) == 0) {
		return SQLPARSER_STATUS_OK;
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
		return status;
	}
	status = sqlparser_replace_proto_string(search.target_slot, value, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		if (mutation_created) {
			sqlparser_handle_remove_identifier_mutation(
				handle,
				mutation_index);
		}
		return status;
	}
	handle->identifier_mutations[mutation_index].slot =
		search.target_slot;
	handle->identifier_mutations[mutation_index].value =
		*search.target_slot;
	relation_group =
		handle->identifier_mutations[mutation_index].relation_group;
	remove_mutation =
		relation_group == 0U &&
		strcmp(
			handle->identifier_mutations[mutation_index].original,
			value) == 0;

	status = sqlparser_handle_commit_ast(handle, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		if (mutation_created) {
			sqlparser_handle_remove_identifier_mutation(
				handle,
				mutation_index);
		}
		return status;
	}
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
