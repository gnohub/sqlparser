#ifndef SQLPARSER_TEST_VIEW_ASSERT_H
#define SQLPARSER_TEST_VIEW_ASSERT_H

#include <jansson.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sqlparser_internal.h"

static const char *sqlparser_test_string_value(json_t *value)
{
	if (!json_is_string(value)) {
		return NULL;
	}
	return json_string_value(value);
}

static const char *sqlparser_test_forbidden_query_graph_key(json_t *node)
{
	const char *key;
	const char *nested_key;
	json_t *value;
	size_t index;

	if (json_is_object(node)) {
		json_object_foreach(node, key, value)
		{
			if (strcmp(key, "source_columns") == 0 ||
			    strcmp(key, "expressions") == 0 ||
			    strcmp(key, "binds") == 0 ||
			    strcmp(key, "sql") == 0 ||
			    strcmp(key, "status") == 0 ||
			    strcmp(key, "reason") == 0 ||
			    strcmp(key, "diagnostics") == 0 ||
			    strcmp(key, "contract") == 0 ||
			    strcmp(key, "version") == 0 ||
			    strcmp(key, "message") == 0 ||
			    strcmp(key, "debug") == 0 ||
			    strcmp(key, "log") == 0) {
				return key;
			}
			nested_key = sqlparser_test_forbidden_query_graph_key(value);
			if (nested_key != NULL) {
				return nested_key;
			}
		}
		return NULL;
	}
	if (json_is_array(node)) {
		json_array_foreach(node, index, value)
		{
			nested_key = sqlparser_test_forbidden_query_graph_key(value);
			if (nested_key != NULL) {
				return nested_key;
			}
		}
	}
	return NULL;
}

static const char *sqlparser_test_null_query_graph_key(json_t *node)
{
	const char *key;
	const char *nested_key;
	json_t *value;
	size_t index;

	if (json_is_null(node)) {
		return "array item";
	}
	if (json_is_object(node)) {
		json_object_foreach(node, key, value)
		{
			if (json_is_null(value)) {
				return key;
			}
			nested_key = sqlparser_test_null_query_graph_key(value);
			if (nested_key != NULL) {
				return nested_key;
			}
		}
		return NULL;
	}
	if (json_is_array(node)) {
		json_array_foreach(node, index, value)
		{
			nested_key = sqlparser_test_null_query_graph_key(value);
			if (nested_key != NULL) {
				return nested_key;
			}
		}
	}
	return NULL;
}

static const char *sqlparser_test_empty_query_graph_array_key(json_t *node)
{
	const char *key;
	const char *nested_key;
	json_t *value;
	size_t index;

	if (json_is_object(node)) {
		json_object_foreach(node, key, value)
		{
			if (json_is_array(value) && json_array_size(value) == 0U) {
				return key;
			}
			nested_key = sqlparser_test_empty_query_graph_array_key(value);
			if (nested_key != NULL) {
				return nested_key;
			}
		}
		return NULL;
	}
	if (json_is_array(node)) {
		json_array_foreach(node, index, value)
		{
			nested_key = sqlparser_test_empty_query_graph_array_key(value);
			if (nested_key != NULL) {
				return nested_key;
			}
		}
	}
	return NULL;
}

static int sqlparser_test_optional_array_is_valid(json_t *object, const char *key)
{
	json_t *value;

	value = json_object_get(object, key);
	return value == NULL || json_is_array(value);
}

static int sqlparser_test_string_is_one_of(
	json_t *value,
	const char *const *allowed,
	size_t allowed_count)
{
	const char *text;
	size_t index;

	text = sqlparser_test_string_value(value);
	if (text == NULL) {
		return 0;
	}
	for (index = 0U; index < allowed_count; index++) {
		if (strcmp(text, allowed[index]) == 0) {
			return 1;
		}
	}
	return 0;
}

static int sqlparser_test_fail_case(const char *case_id, const char *case_name, const char *message)
{
	fprintf(stderr,
	        "FAIL [%s %s]: %s\n",
	        case_id != NULL ? case_id : "-",
	        case_name != NULL ? case_name : "-",
	        message != NULL ? message : "view assertion failed");
	return 1;
}

static int sqlparser_test_fail_case_field(
	const char *case_id,
	const char *case_name,
	const char *field_name,
	const char *expected)
{
	fprintf(stderr,
	        "FAIL [%s %s]: missing %s value '%s'\n",
	        case_id != NULL ? case_id : "-",
	        case_name != NULL ? case_name : "-",
	        field_name != NULL ? field_name : "-",
	        expected != NULL ? expected : "null");
	return 1;
}

static int sqlparser_test_text_contains_expected(
	const char *case_id,
	const char *case_name,
	const char *text,
	const char *field_name,
	json_t *expected_value)
{
	size_t index;
	json_t *item;
	const char *expected;

	if (expected_value == NULL) {
		return 0;
	}
	if (json_is_string(expected_value)) {
		expected = json_string_value(expected_value);
		return text != NULL && expected != NULL && strstr(text, expected) != NULL ?
			0 :
			sqlparser_test_fail_case_field(case_id, case_name, field_name, expected);
	}
	if (!json_is_array(expected_value)) {
		return sqlparser_test_fail_case(case_id, case_name, "expected text assertion must be a string or array");
	}
	json_array_foreach(expected_value, index, item)
	{
		expected = sqlparser_test_string_value(item);
		if (expected == NULL) {
			return sqlparser_test_fail_case(case_id, case_name, "expected text assertion item must be a string");
		}
		if (text == NULL || strstr(text, expected) == NULL) {
			return sqlparser_test_fail_case_field(case_id, case_name, field_name, expected);
		}
	}
	return 0;
}

static int sqlparser_test_text_not_contains_expected(
	const char *case_id,
	const char *case_name,
	const char *text,
	const char *field_name,
	json_t *expected_value)
{
	size_t index;
	json_t *item;
	const char *expected;

	if (expected_value == NULL) {
		return 0;
	}
	if (json_is_string(expected_value)) {
		expected = json_string_value(expected_value);
		return text == NULL || expected == NULL || strstr(text, expected) == NULL ?
			0 :
			sqlparser_test_fail_case_field(case_id, case_name, field_name, expected);
	}
	if (!json_is_array(expected_value)) {
		return sqlparser_test_fail_case(case_id, case_name, "expected text assertion must be a string or array");
	}
	json_array_foreach(expected_value, index, item)
	{
		expected = sqlparser_test_string_value(item);
		if (expected == NULL) {
			return sqlparser_test_fail_case(case_id, case_name, "expected text assertion item must be a string");
		}
		if (text != NULL && strstr(text, expected) != NULL) {
			return sqlparser_test_fail_case_field(case_id, case_name, field_name, expected);
		}
	}
	return 0;
}

static int sqlparser_test_verify_exact_deparse(
	const char *case_id,
	const char *case_name,
	const char *original_sql,
	const char *deparse_sql)
{
	size_t index;

	if (original_sql == NULL || deparse_sql == NULL) {
		return sqlparser_test_fail_case(
			case_id,
			case_name,
			"deparse text is missing");
	}
	if (strcmp(original_sql, deparse_sql) == 0) {
		return 0;
	}
	for (index = 0U;
	     original_sql[index] != '\0' &&
	     deparse_sql[index] != '\0' &&
	     original_sql[index] == deparse_sql[index];
	     index++) {
		/* Find the first byte that changed. */
	}
	fprintf(
		stderr,
		"FAIL [%s %s]: deparse changed original SQL at byte %lu\n",
		case_id != NULL ? case_id : "-",
		case_name != NULL ? case_name : "-",
		(unsigned long)index);
	return 1;
}

static int sqlparser_test_verify_ast_identifier_spelling(
	const char *case_id,
	const char *case_name,
	sqlparser_handle_t *handle)
{
	sqlparser_error_t error;
	sqlparser_status_t status;

	if (handle == NULL) {
		return sqlparser_test_fail_case(
			case_id,
			case_name,
			"AST spelling audit requires a parsed handle");
	}
	memset(&error, 0, sizeof(error));
	status = sqlparser_validate_ast_identifier_spelling(handle, &error);
	if (status != SQLPARSER_STATUS_OK) {
		fprintf(
			stderr,
			"FAIL [%s %s]: %s\n",
			case_id != NULL ? case_id : "-",
			case_name != NULL ? case_name : "-",
			error.message[0] != '\0' ?
				error.message :
				"AST identifier spelling audit failed");
		return 1;
	}
	return 0;
}

static int sqlparser_test_merge_inserted_assignment_graph_matches(
	const sqlparser_handle_t *handle,
	size_t dml_index,
	size_t assignment_ordinal,
	size_t original_assignment_count,
	const sqlparser_selector_t *selector,
	const sqlparser_graph_dml_assignment_t *original_assignment)
{
	char expected_shifted_bind[SQLPARSER_BIND_TEXT_CAPACITY];
	sqlparser_error_t error;
	sqlparser_graph_dml_assignment_t inserted_assignment;
	sqlparser_graph_dml_assignment_t shifted_assignment;
	sqlparser_graph_dml_t dml;
	sqlparser_graph_field_t inserted_source_field;
	sqlparser_graph_field_t shifted_source_field;
	sqlparser_graph_target_t inserted_source_target;
	sqlparser_graph_target_t shifted_source_target;
	sqlparser_query_graph_view_t graph;
	size_t inserted_index;
	size_t shifted_index;
	size_t shifted_position;
	int question_bind;
	int status;

	if (handle == NULL || selector == NULL || original_assignment == NULL ||
	    original_assignment_count == (size_t)-1 ||
	    assignment_ordinal == (size_t)-1 ||
	    selector->column_index == (size_t)-1) {
		return 0;
	}
	memset(&error, 0, sizeof(error));
	memset(&inserted_assignment, 0, sizeof(inserted_assignment));
	memset(&shifted_assignment, 0, sizeof(shifted_assignment));
	memset(&dml, 0, sizeof(dml));
	memset(&inserted_source_field, 0, sizeof(inserted_source_field));
	memset(&shifted_source_field, 0, sizeof(shifted_source_field));
	memset(&inserted_source_target, 0, sizeof(inserted_source_target));
	memset(&shifted_source_target, 0, sizeof(shifted_source_target));
	memset(&graph, 0, sizeof(graph));
	status = sqlparser_statement_query_graph(
		handle,
		selector->statement_index,
		&graph,
		&error);
	if (status != SQLPARSER_STATUS_OK) {
		return 0;
	}
	status = sqlparser_query_graph_dml_at(&graph, dml_index, &dml, &error);
	if (status != SQLPARSER_STATUS_OK ||
	    dml.kind != SQLPARSER_GRAPH_DML_MERGE ||
	    dml.assignments.count != original_assignment_count + 1U) {
		return 0;
	}
	status = sqlparser_query_graph_span_index_at(
		&graph,
		dml.assignments,
		assignment_ordinal,
		&inserted_index,
		&error);
	if (status != SQLPARSER_STATUS_OK) {
		return 0;
	}
	status = sqlparser_query_graph_span_index_at(
		&graph,
		dml.assignments,
		assignment_ordinal + 1U,
		&shifted_index,
		&error);
	if (status != SQLPARSER_STATUS_OK) {
		return 0;
	}
	status = sqlparser_query_graph_dml_assignment_at(
		&graph,
		inserted_index,
		&inserted_assignment,
		&error);
	if (status != SQLPARSER_STATUS_OK) {
		return 0;
	}
	status = sqlparser_query_graph_dml_assignment_at(
		&graph,
		shifted_index,
		&shifted_assignment,
		&error);
	if (status != SQLPARSER_STATUS_OK ||
	    !inserted_assignment.has_selector ||
	    inserted_assignment.selector.kind != selector->kind ||
	    inserted_assignment.selector.statement_index !=
		    selector->statement_index ||
	    inserted_assignment.selector.item_index != selector->item_index ||
	    inserted_assignment.selector.column_index !=
		    selector->column_index ||
	    !shifted_assignment.has_selector ||
	    shifted_assignment.selector.kind != selector->kind ||
	    shifted_assignment.selector.statement_index !=
		    selector->statement_index ||
	    shifted_assignment.selector.item_index != selector->item_index ||
	    shifted_assignment.selector.column_index !=
		    selector->column_index + 1U ||
	    inserted_assignment.value_kind != original_assignment->value_kind ||
	    shifted_assignment.value_kind != original_assignment->value_kind ||
	    inserted_assignment.has_source_target !=
		    original_assignment->has_source_target ||
	    shifted_assignment.has_source_target !=
		    original_assignment->has_source_target ||
	    inserted_assignment.has_source_field !=
		    original_assignment->has_source_field ||
	    shifted_assignment.has_source_field !=
		    original_assignment->has_source_field ||
	    inserted_assignment.has_bind != original_assignment->has_bind ||
	    shifted_assignment.has_bind != original_assignment->has_bind ||
	    inserted_assignment.has_bind_sql !=
		    original_assignment->has_bind_sql ||
	    shifted_assignment.has_bind_sql !=
		    original_assignment->has_bind_sql ||
	    inserted_assignment.has_bind_position !=
		    original_assignment->has_bind_position ||
	    shifted_assignment.has_bind_position !=
		    original_assignment->has_bind_position) {
		return 0;
	}
	if (inserted_assignment.has_source_target) {
		status = sqlparser_query_graph_target_at(
			&graph,
			inserted_assignment.source_target_index,
			&inserted_source_target,
			&error);
		if (status != SQLPARSER_STATUS_OK) {
			return 0;
		}
		status = sqlparser_query_graph_target_at(
			&graph,
			shifted_assignment.source_target_index,
			&shifted_source_target,
			&error);
		if (status != SQLPARSER_STATUS_OK ||
		    inserted_source_target.kind != shifted_source_target.kind ||
		    inserted_source_target.block_index !=
			    shifted_source_target.block_index ||
		    inserted_source_target.ordinal !=
			    shifted_source_target.ordinal ||
		    ((inserted_source_target.output_name == NULL) !=
		     (shifted_source_target.output_name == NULL)) ||
		    (inserted_source_target.output_name != NULL &&
		     strcmp(
			     inserted_source_target.output_name,
			     shifted_source_target.output_name) != 0)) {
			return 0;
		}
	}
	if (inserted_assignment.has_source_field) {
		status = sqlparser_query_graph_field_at(
			&graph,
			inserted_assignment.source_field_index,
			&inserted_source_field,
			&error);
		if (status != SQLPARSER_STATUS_OK) {
			return 0;
		}
		status = sqlparser_query_graph_field_at(
			&graph,
			shifted_assignment.source_field_index,
			&shifted_source_field,
			&error);
		if (status != SQLPARSER_STATUS_OK ||
		    inserted_source_field.block_index !=
			    shifted_source_field.block_index ||
		    inserted_source_field.has_relation !=
			    shifted_source_field.has_relation ||
		    (inserted_source_field.has_relation &&
		     inserted_source_field.relation_index !=
			     shifted_source_field.relation_index) ||
		    ((inserted_source_field.column_name == NULL) !=
		     (shifted_source_field.column_name == NULL)) ||
		    (inserted_source_field.column_name != NULL &&
		     strcmp(
			     inserted_source_field.column_name,
			     shifted_source_field.column_name) != 0)) {
			return 0;
		}
	}
	if (original_assignment->has_bind_sql &&
	    (strcmp(
		     inserted_assignment.bind_sql,
		     original_assignment->bind_sql) != 0 ||
	     strcmp(
		     shifted_assignment.bind_sql,
		     original_assignment->bind_sql) != 0)) {
		return 0;
	}
	if (original_assignment->has_bind_position) {
		if (original_assignment->bind_position == (size_t)-1) {
			return 0;
		}
		shifted_position = original_assignment->bind_position + 1U;
		if (inserted_assignment.bind_position !=
			    original_assignment->bind_position ||
		    shifted_assignment.bind_position != shifted_position) {
			return 0;
		}
	} else {
		shifted_position = 0U;
	}
	if (!original_assignment->has_bind) {
		return 1;
	}
	if (inserted_assignment.bind_kind != original_assignment->bind_kind ||
	    shifted_assignment.bind_kind != original_assignment->bind_kind ||
	    strcmp(inserted_assignment.bind, original_assignment->bind) != 0) {
		return 0;
	}
	question_bind =
		original_assignment->has_bind_sql &&
		strcmp(original_assignment->bind_sql, "?") == 0;
	if (!question_bind) {
		return strcmp(
			       shifted_assignment.bind,
			       original_assignment->bind) == 0;
	}
	if (!original_assignment->has_bind_position) {
		return 0;
	}
	(void)snprintf(
		expected_shifted_bind,
		sizeof(expected_shifted_bind),
		"%lu",
		(unsigned long)shifted_position);
	return strcmp(shifted_assignment.bind, expected_shifted_bind) == 0;
}

static int sqlparser_test_deparse_reparses(
	const sqlparser_handle_t *handle)
{
	sqlparser_parse_options_t options;
	sqlparser_error_t error;
	sqlparser_handle_t *reparsed;
	char *sql;
	int status;

	if (handle == NULL) {
		return 0;
	}
	reparsed = NULL;
	sql = NULL;
	memset(&error, 0, sizeof(error));
	status = sqlparser_deparse(handle, &sql, &error);
	if (status == SQLPARSER_STATUS_OK && sql != NULL) {
		sqlparser_parse_options_default(&options);
		options.dialect = sqlparser_handle_dialect(handle);
		status = sqlparser_parse_with_options(
			sql,
			&options,
			&reparsed,
			&error);
		if (status == SQLPARSER_STATUS_OK && reparsed == NULL) {
			status = SQLPARSER_STATUS_INTERNAL_ERROR;
		}
	} else if (status == SQLPARSER_STATUS_OK) {
		status = SQLPARSER_STATUS_INTERNAL_ERROR;
	}
	sqlparser_handle_destroy(reparsed);
	sqlparser_string_free(sql);
	return status == SQLPARSER_STATUS_OK;
}

static int sqlparser_test_verify_one_merge_assignment_mutation(
	const char *case_id,
	const char *case_name,
	const sqlparser_handle_t *handle,
	size_t dml_index,
	size_t assignment_ordinal,
	size_t original_assignment_count,
	const sqlparser_graph_dml_assignment_t *original_assignment)
{
	static const char replace_prefix[] = "sqlparser_probe2 = ";
	static const char insert_prefix[] = "sqlparser_probe3 = ";
	const char *probe_part;
	const char *failure;
	sqlparser_assignment_view_t assignment_view;
	sqlparser_error_t error;
	sqlparser_graph_dml_assignment_t final_assignment;
	sqlparser_graph_dml_t final_dml;
	sqlparser_identifier_path_view_t target;
	sqlparser_parse_options_t options;
	sqlparser_patch_list_t patches;
	sqlparser_patch_t patch;
	sqlparser_query_graph_view_t final_graph;
	sqlparser_selector_t parsed_selector;
	sqlparser_selector_t selector;
	sqlparser_handle_t *candidate;
	sqlparser_handle_t *reparsed;
	char *assignment_sql;
	char *current_sql;
	char *final_rhs_sql;
	char *rhs_sql;
	char *selector_text;
	size_t assignment_index;
	size_t assignment_sql_size;
	int status;

	if (handle == NULL || original_assignment == NULL) {
		return sqlparser_test_fail_case(
			case_id,
			case_name,
			"MERGE assignment mutation audit input is invalid");
	}
	selector = original_assignment->selector;
	probe_part = "sqlparser_probe1";
	failure = NULL;
	candidate = NULL;
	reparsed = NULL;
	assignment_sql = NULL;
	current_sql = NULL;
	final_rhs_sql = NULL;
	rhs_sql = NULL;
	selector_text = NULL;
	memset(&assignment_view, 0, sizeof(assignment_view));
	memset(&error, 0, sizeof(error));
	memset(&final_assignment, 0, sizeof(final_assignment));
	memset(&final_dml, 0, sizeof(final_dml));
	memset(&target, 0, sizeof(target));
	memset(&patch, 0, sizeof(patch));
	memset(&final_graph, 0, sizeof(final_graph));
	memset(&parsed_selector, 0, sizeof(parsed_selector));

	status = sqlparser_selector_format(&selector, &selector_text, &error);
	if (status != SQLPARSER_STATUS_OK) {
		failure = "MERGE assignment selector format failed";
		goto done;
	}
	status = sqlparser_selector_parse(selector_text, &parsed_selector, &error);
	if (status != SQLPARSER_STATUS_OK ||
	    parsed_selector.kind != selector.kind ||
	    parsed_selector.statement_index != selector.statement_index ||
	    parsed_selector.item_index != selector.item_index ||
	    parsed_selector.column_index != selector.column_index) {
		failure = "MERGE assignment selector round-trip failed";
		goto done;
	}
	status = sqlparser_selector_update_assignment(
		handle,
		&selector,
		&assignment_view,
		&error);
	if (status != SQLPARSER_STATUS_OK ||
	    assignment_view.column_name == NULL ||
	    assignment_view.column_name[0] == '\0') {
		failure = "MERGE assignment selector read failed";
		goto done;
	}
	status = sqlparser_selector_update_assignment_sql(
		handle,
		&selector,
		&rhs_sql,
		&error);
	if (status != SQLPARSER_STATUS_OK ||
	    rhs_sql == NULL ||
	    rhs_sql[0] == '\0') {
		failure = "MERGE assignment RHS read failed";
		goto done;
	}
	status = sqlparser_handle_clone(handle, &candidate, &error);
	if (status != SQLPARSER_STATUS_OK) {
		failure = "MERGE assignment handle clone failed";
		goto done;
	}

	patches.items = &patch;
	patches.count = 1U;
	patch.op = SQLPARSER_PATCH_REPLACE;
	patch.selector = selector_text;
	patch.sql = rhs_sql;
	status = sqlparser_apply_patch(candidate, &patches, &error);
	if (status != SQLPARSER_STATUS_OK) {
		failure = "MERGE assignment same-RHS replace failed";
		goto done;
	}

	target.parts = &probe_part;
	target.part_count = 1U;
	status = sqlparser_selector_insert_update_assignment_from_assignment_value(
		candidate,
		&selector,
		&target,
		&selector,
		&error);
	if (status != SQLPARSER_STATUS_OK) {
		failure = "MERGE structured assignment insertion failed";
		goto done;
	}
	status = sqlparser_selector_update_assignment(
		candidate,
		&selector,
		&assignment_view,
		&error);
	if (status != SQLPARSER_STATUS_OK ||
	    assignment_view.column_name == NULL ||
	    strcmp(assignment_view.column_name, probe_part) != 0) {
		failure = "MERGE structured assignment insertion is not addressable";
		goto done;
	}
	if (!sqlparser_test_merge_inserted_assignment_graph_matches(
		    candidate,
		    dml_index,
		    assignment_ordinal,
		    original_assignment_count,
		    &selector,
		    original_assignment)) {
		failure = "MERGE structured assignment insertion changed value metadata";
		goto done;
	}
	if (!sqlparser_test_deparse_reparses(candidate)) {
		failure = "MERGE structured assignment insertion does not reparse";
		goto done;
	}

	if (strlen(rhs_sql) >
	    (size_t)-1 - sizeof(replace_prefix)) {
		failure = "MERGE assignment fragment is too large";
		goto done;
	}
	assignment_sql_size = sizeof(replace_prefix) + strlen(rhs_sql);
	assignment_sql = (char *)malloc(assignment_sql_size);
	if (assignment_sql == NULL) {
		failure = "MERGE assignment fragment allocation failed";
		goto done;
	}
	(void)snprintf(
		assignment_sql,
		assignment_sql_size,
		"%s%s",
		replace_prefix,
		rhs_sql);
	memset(&patch, 0, sizeof(patch));
	patch.op = SQLPARSER_PATCH_REPLACE_ASSIGNMENT;
	patch.selector = selector_text;
	patch.sql = assignment_sql;
	status = sqlparser_apply_patch(candidate, &patches, &error);
	if (status != SQLPARSER_STATUS_OK) {
		failure = "MERGE full assignment replace failed";
		goto done;
	}
	status = sqlparser_selector_update_assignment(
		candidate,
		&selector,
		&assignment_view,
		&error);
	if (status != SQLPARSER_STATUS_OK ||
	    assignment_view.column_name == NULL ||
	    strcmp(assignment_view.column_name, "sqlparser_probe2") != 0) {
		failure = "MERGE full assignment replacement is not addressable";
		goto done;
	}
	if (!sqlparser_test_merge_inserted_assignment_graph_matches(
		    candidate,
		    dml_index,
		    assignment_ordinal,
		    original_assignment_count,
		    &selector,
		    original_assignment)) {
		failure = "MERGE full assignment replacement changed value metadata";
		goto done;
	}
	status = sqlparser_selector_update_assignment_sql(
		candidate,
		&selector,
		&final_rhs_sql,
		&error);
	if (status != SQLPARSER_STATUS_OK ||
	    final_rhs_sql == NULL ||
	    strcmp(final_rhs_sql, rhs_sql) != 0) {
		failure = "MERGE full assignment replacement changed its RHS";
		goto done;
	}
	sqlparser_string_free(final_rhs_sql);
	final_rhs_sql = NULL;
	if (!sqlparser_test_deparse_reparses(candidate)) {
		failure = "MERGE full assignment replacement does not reparse";
		goto done;
	}
	free(assignment_sql);
	assignment_sql = NULL;

	memset(&patch, 0, sizeof(patch));
	patch.op = SQLPARSER_PATCH_DELETE_ASSIGNMENT;
	patch.selector = selector_text;
	status = sqlparser_apply_patch(candidate, &patches, &error);
	if (status != SQLPARSER_STATUS_OK) {
		failure = "MERGE assignment delete failed";
		goto done;
	}

	if (strlen(rhs_sql) >
	    (size_t)-1 - sizeof(insert_prefix)) {
		failure = "MERGE assignment fragment is too large";
		goto done;
	}
	assignment_sql_size = sizeof(insert_prefix) + strlen(rhs_sql);
	assignment_sql = (char *)malloc(assignment_sql_size);
	if (assignment_sql == NULL) {
		failure = "MERGE assignment fragment allocation failed";
		goto done;
	}
	(void)snprintf(
		assignment_sql,
		assignment_sql_size,
		"%s%s",
		insert_prefix,
		rhs_sql);
	memset(&patch, 0, sizeof(patch));
	patch.op = SQLPARSER_PATCH_INSERT_ASSIGNMENT;
	patch.selector = selector_text;
	patch.sql = assignment_sql;
	status = sqlparser_apply_patch(candidate, &patches, &error);
	if (status != SQLPARSER_STATUS_OK) {
		failure = "MERGE SQL assignment insertion failed";
		goto done;
	}
	status = sqlparser_selector_update_assignment(
		candidate,
		&selector,
		&assignment_view,
		&error);
	if (status != SQLPARSER_STATUS_OK ||
	    assignment_view.column_name == NULL ||
	    strcmp(assignment_view.column_name, "sqlparser_probe3") != 0) {
		failure = "MERGE SQL assignment insertion is not addressable";
		goto done;
	}
	if (!sqlparser_test_merge_inserted_assignment_graph_matches(
		    candidate,
		    dml_index,
		    assignment_ordinal,
		    original_assignment_count,
		    &selector,
		    original_assignment)) {
		failure = "MERGE SQL assignment insertion changed value metadata";
		goto done;
	}
	if (!sqlparser_test_deparse_reparses(candidate)) {
		failure = "MERGE SQL assignment insertion does not reparse";
		goto done;
	}
	free(assignment_sql);
	assignment_sql = NULL;

	memset(&patch, 0, sizeof(patch));
	patch.op = SQLPARSER_PATCH_DELETE_ASSIGNMENT;
	patch.selector = selector_text;
	status = sqlparser_apply_patch(candidate, &patches, &error);
	if (status != SQLPARSER_STATUS_OK) {
		failure = "MERGE inserted assignment delete failed";
		goto done;
	}

	status = sqlparser_statement_query_graph(
		candidate,
		selector.statement_index,
		&final_graph,
		&error);
	if (status != SQLPARSER_STATUS_OK) {
		failure = "MERGE graph rebuild after assignment mutations failed";
		goto done;
	}
	status = sqlparser_query_graph_dml_at(
		&final_graph,
		dml_index,
		&final_dml,
		&error);
	if (status != SQLPARSER_STATUS_OK ||
	    final_dml.kind != SQLPARSER_GRAPH_DML_MERGE ||
	    final_dml.assignments.count != original_assignment_count) {
		failure = "MERGE assignment count changed after reversible mutations";
		goto done;
	}
	status = sqlparser_query_graph_span_index_at(
		&final_graph,
		final_dml.assignments,
		assignment_ordinal,
		&assignment_index,
		&error);
	if (status != SQLPARSER_STATUS_OK) {
		failure = "MERGE final assignment span lookup failed";
		goto done;
	}
	status = sqlparser_query_graph_dml_assignment_at(
		&final_graph,
		assignment_index,
		&final_assignment,
		&error);
	if (status != SQLPARSER_STATUS_OK ||
	    !final_assignment.has_selector ||
	    final_assignment.selector.kind !=
		    SQLPARSER_SELECTOR_KIND_MERGE_ASSIGNMENT ||
	    final_assignment.selector.statement_index !=
		    selector.statement_index ||
	    final_assignment.selector.item_index != selector.item_index ||
	    final_assignment.selector.column_index != selector.column_index ||
	    final_assignment.value_kind != original_assignment->value_kind ||
	    final_assignment.has_source_target !=
		    original_assignment->has_source_target ||
	    (final_assignment.has_source_target &&
	     final_assignment.source_target_index !=
		     original_assignment->source_target_index) ||
	    final_assignment.has_source_field !=
		    original_assignment->has_source_field ||
	    (final_assignment.has_source_field &&
	     final_assignment.source_field_index !=
		     original_assignment->source_field_index) ||
	    final_assignment.has_bind != original_assignment->has_bind ||
	    final_assignment.has_bind_sql != original_assignment->has_bind_sql ||
	    final_assignment.has_bind_position !=
		    original_assignment->has_bind_position ||
	    (final_assignment.has_bind &&
	     (final_assignment.bind_kind != original_assignment->bind_kind ||
	      strcmp(final_assignment.bind, original_assignment->bind) != 0)) ||
	    (final_assignment.has_bind_sql &&
	     strcmp(
		     final_assignment.bind_sql,
		     original_assignment->bind_sql) != 0) ||
	    (final_assignment.has_bind_position &&
	     final_assignment.bind_position !=
		     original_assignment->bind_position)) {
		failure = "MERGE assignment graph changed after reversible mutations";
		goto done;
	}
	status = sqlparser_selector_update_assignment_sql(
		candidate,
		&selector,
		&final_rhs_sql,
		&error);
	if (status != SQLPARSER_STATUS_OK ||
	    final_rhs_sql == NULL ||
	    strcmp(final_rhs_sql, rhs_sql) != 0) {
		failure = "MERGE assignment RHS changed after reversible mutations";
		goto done;
	}
	if (sqlparser_test_verify_ast_identifier_spelling(
		    case_id,
		    case_name,
		    candidate) != 0) {
		failure = "MERGE assignment mutation changed identifier spelling";
		goto done;
	}

	status = sqlparser_deparse(candidate, &current_sql, &error);
	if (status != SQLPARSER_STATUS_OK ||
	    current_sql == NULL ||
	    current_sql[0] == '\0') {
		failure = "MERGE assignment mutation deparse failed";
		goto done;
	}
	sqlparser_parse_options_default(&options);
	options.dialect = sqlparser_handle_dialect(candidate);
	status = sqlparser_parse_with_options(
		current_sql,
		&options,
		&reparsed,
		&error);
	if (status != SQLPARSER_STATUS_OK || reparsed == NULL) {
		failure = "MERGE assignment mutation output does not reparse";
		goto done;
	}
	if (sqlparser_test_verify_ast_identifier_spelling(
		    case_id,
		    case_name,
		    reparsed) != 0) {
		failure = "reparsed MERGE assignment changed identifier spelling";
		goto done;
	}

done:
	if (failure != NULL) {
		fprintf(
			stderr,
			"FAIL [%s %s]: %s (%s)%s%s\n",
			case_id != NULL ? case_id : "-",
			case_name != NULL ? case_name : "-",
			failure,
			selector_text != NULL ? selector_text : "unknown selector",
			error.message[0] != '\0' ? ": " : "",
			error.message[0] != '\0' ? error.message : "");
	}
	sqlparser_handle_destroy(reparsed);
	sqlparser_handle_destroy(candidate);
	sqlparser_string_free(current_sql);
	sqlparser_string_free(final_rhs_sql);
	sqlparser_string_free(rhs_sql);
	sqlparser_string_free(selector_text);
	free(assignment_sql);
	return failure != NULL ? 1 : 0;
}

static int sqlparser_test_verify_merge_assignment_mutations(
	const char *case_id,
	const char *case_name,
	const sqlparser_handle_t *handle)
{
	sqlparser_error_t error;
	sqlparser_graph_dml_assignment_t assignment;
	sqlparser_graph_dml_t dml;
	sqlparser_query_graph_view_t graph;
	size_t assignment_index;
	size_t assignment_ordinal;
	size_t dml_count;
	size_t dml_index;
	size_t statement_index;
	int status;

	if (handle == NULL) {
		return sqlparser_test_fail_case(
			case_id,
			case_name,
			"MERGE assignment mutation audit requires a parsed handle");
	}
	memset(&error, 0, sizeof(error));
	for (statement_index = 0U;
	     statement_index < sqlparser_statement_count(handle);
	     statement_index++) {
		memset(&graph, 0, sizeof(graph));
		status = sqlparser_statement_query_graph(
			handle,
			statement_index,
			&graph,
			&error);
		if (status != SQLPARSER_STATUS_OK) {
			return sqlparser_test_fail_case(
				case_id,
				case_name,
				"query graph build failed during MERGE assignment audit");
		}
		if (!graph.has_dml) {
			continue;
		}
		status = sqlparser_query_graph_dml_count(
			&graph,
			&dml_count,
			&error);
		if (status != SQLPARSER_STATUS_OK) {
			return sqlparser_test_fail_case(
				case_id,
				case_name,
				"DML count failed during MERGE assignment audit");
		}
		for (dml_index = 0U; dml_index < dml_count; dml_index++) {
			memset(&dml, 0, sizeof(dml));
			status = sqlparser_query_graph_dml_at(
				&graph,
				dml_index,
				&dml,
				&error);
			if (status != SQLPARSER_STATUS_OK) {
				return sqlparser_test_fail_case(
					case_id,
					case_name,
					"DML lookup failed during MERGE assignment audit");
			}
			if (dml.kind != SQLPARSER_GRAPH_DML_MERGE) {
				continue;
			}
			for (assignment_ordinal = 0U;
			     assignment_ordinal < dml.assignments.count;
			     assignment_ordinal++) {
				status = sqlparser_query_graph_span_index_at(
					&graph,
					dml.assignments,
					assignment_ordinal,
					&assignment_index,
					&error);
				if (status != SQLPARSER_STATUS_OK) {
					return sqlparser_test_fail_case(
						case_id,
						case_name,
						"MERGE assignment span lookup failed");
				}
				memset(&assignment, 0, sizeof(assignment));
				status = sqlparser_query_graph_dml_assignment_at(
					&graph,
					assignment_index,
					&assignment,
					&error);
				if (status != SQLPARSER_STATUS_OK ||
				    !assignment.has_selector ||
				    assignment.selector.kind !=
					    SQLPARSER_SELECTOR_KIND_MERGE_ASSIGNMENT) {
					return sqlparser_test_fail_case(
						case_id,
						case_name,
						"MERGE assignment selector is missing");
				}
				if (sqlparser_test_verify_one_merge_assignment_mutation(
					    case_id,
					    case_name,
					    handle,
					    dml_index,
					    assignment_ordinal,
					    dml.assignments.count,
					    &assignment) != 0) {
					return 1;
				}
			}
		}
	}
	return 0;
}

static int sqlparser_test_verify_session_value_shape(
	const char *case_id,
	const char *case_name,
	json_t *value)
{
	static const char *const kinds[] = {
		"identifier",
		"keyword",
		"literal",
		"bind",
		"expression"
	};
	const char *kind;
	json_t *field;

	if (!json_is_object(value) ||
	    !sqlparser_test_string_is_one_of(
		    json_object_get(value, "kind"),
		    kinds,
		    sizeof(kinds) / sizeof(kinds[0]))) {
		return sqlparser_test_fail_case(case_id, case_name, "session value kind is invalid");
	}
	field = json_object_get(value, "name");
	if (field != NULL &&
	    (!json_is_string(field) || json_string_length(field) == 0U)) {
		return sqlparser_test_fail_case(case_id, case_name, "session value name is invalid");
	}
	kind = json_string_value(json_object_get(value, "kind"));
	if (strcmp(kind, "literal") == 0) {
		if (!json_is_object(json_object_get(value, "literal"))) {
			return sqlparser_test_fail_case(case_id, case_name, "session literal value is missing");
		}
		return 0;
	}
	if (strcmp(kind, "bind") == 0) {
		json_int_t bind_kind;
		json_int_t bind_position;

		field = json_object_get(value, "bind_kind");
		if (!json_is_integer(field)) {
			return sqlparser_test_fail_case(case_id, case_name, "session bind kind is missing");
		}
		bind_kind = json_integer_value(field);
		field = json_object_get(value, "bind_position");
		if (!json_is_integer(field)) {
			return sqlparser_test_fail_case(case_id, case_name, "session bind position is missing");
		}
		bind_position = json_integer_value(field);
		if ((bind_kind != 1 && bind_kind != 2) ||
		    bind_position <= 0 ||
		    !json_is_string(json_object_get(value, "bind_key")) ||
		    json_string_length(json_object_get(value, "bind_key")) == 0U ||
		    !json_is_string(json_object_get(value, "bind_sql")) ||
		    json_string_length(json_object_get(value, "bind_sql")) == 0U) {
			return sqlparser_test_fail_case(case_id, case_name, "session bind value is invalid");
		}
		return 0;
	}
	field = json_object_get(value, "text");
	if (!json_is_string(field) || json_string_length(field) == 0U) {
		return sqlparser_test_fail_case(case_id, case_name, "session text value is missing");
	}
	return 0;
}

static int sqlparser_test_verify_session_shape(
	const char *case_id,
	const char *case_name,
	json_t *session)
{
	static const char *const actions[] = {
		"set",
		"reset",
		"switch",
		"discard",
		"enable",
		"disable",
		"force",
		"advise",
		"close",
		"sync",
		"assume",
		"revert"
	};
	static const char *const scopes[] = {
		"session",
		"local",
		"transaction"
	};
	static const char *const targets[] = {
		"parameter",
		"variable",
		"database",
		"schema",
		"container",
		"role",
		"authorization",
		"login",
		"user",
		"transaction",
		"session_context",
		"database_link",
		"object",
		"constraint",
		"all"
	};
	json_t *items;
	json_t *item;
	size_t item_index;

	if (!json_is_object(session) ||
	    !sqlparser_test_string_is_one_of(
		    json_object_get(session, "action"),
		    actions,
		    sizeof(actions) / sizeof(actions[0]))) {
		return sqlparser_test_fail_case(case_id, case_name, "session action is invalid");
	}
	items = json_object_get(session, "items");
	if (!json_is_array(items) || json_array_size(items) == 0U) {
		return sqlparser_test_fail_case(case_id, case_name, "session items are missing");
	}
	json_array_foreach(items, item_index, item)
	{
		json_t *name;
		json_t *values;
		json_t *value;
		size_t value_index;

		if (!json_is_object(item) ||
		    !sqlparser_test_string_is_one_of(
			    json_object_get(item, "scope"),
			    scopes,
			    sizeof(scopes) / sizeof(scopes[0])) ||
		    !sqlparser_test_string_is_one_of(
			    json_object_get(item, "target_kind"),
			    targets,
			    sizeof(targets) / sizeof(targets[0]))) {
			return sqlparser_test_fail_case(case_id, case_name, "session item is invalid");
		}
		name = json_object_get(item, "name");
		if (name != NULL &&
		    (!json_is_string(name) || json_string_length(name) == 0U)) {
			return sqlparser_test_fail_case(case_id, case_name, "session item name is invalid");
		}
		values = json_object_get(item, "values");
		if (values == NULL) {
			continue;
		}
		if (!json_is_array(values) || json_array_size(values) == 0U) {
			return sqlparser_test_fail_case(case_id, case_name, "session values are invalid");
		}
		json_array_foreach(values, value_index, value)
		{
			if (sqlparser_test_verify_session_value_shape(
				    case_id, case_name, value) != 0) {
				return 1;
			}
		}
	}
	return 0;
}

static int sqlparser_test_verify_query_graph_shape(
	const char *case_id,
	const char *case_name,
	json_t *statement)
{
	json_t *graph;
	const char *forbidden_key;
	const char *null_key;
	const char *empty_array_key;

	if (!json_is_object(statement)) {
		return sqlparser_test_fail_case(case_id, case_name, "view JSON statement should be an object");
	}
	if (!json_is_integer(json_object_get(statement, "index"))) {
		return sqlparser_test_fail_case(case_id, case_name, "view JSON statement index should be an integer");
	}
	if (!json_is_string(json_object_get(statement, "keyword"))) {
		return sqlparser_test_fail_case(case_id, case_name, "view JSON statement keyword should be a string");
	}
	if (json_object_get(statement, "keywords") != NULL ||
	    json_object_get(statement, "clauses") != NULL ||
	    json_object_get(statement, "objects") != NULL) {
		return sqlparser_test_fail_case(case_id, case_name, "view JSON statement contains removed fields");
	}
	graph = json_object_get(statement, "query_graph");
	if (!json_is_object(graph)) {
		return sqlparser_test_fail_case(case_id, case_name, "view JSON statement should expose query_graph");
	}
	if (!sqlparser_test_optional_array_is_valid(graph, "blocks") ||
	    !sqlparser_test_optional_array_is_valid(graph, "relations") ||
	    !sqlparser_test_optional_array_is_valid(graph, "targets") ||
	    !sqlparser_test_optional_array_is_valid(graph, "fields") ||
	    !sqlparser_test_optional_array_is_valid(graph, "values") ||
	    !sqlparser_test_optional_array_is_valid(graph, "sets") ||
	    !sqlparser_test_optional_array_is_valid(graph, "predicates")) {
		return sqlparser_test_fail_case(case_id, case_name, "query_graph arrays have invalid shape");
	}
	if (json_object_get(graph, "objects") != NULL ||
	    json_object_get(graph, "clauses") != NULL) {
		return sqlparser_test_fail_case(case_id, case_name, "query_graph contains removed fields");
	}
	if (json_object_get(graph, "session") != NULL &&
	    sqlparser_test_verify_session_shape(
		    case_id,
		    case_name,
		    json_object_get(graph, "session")) != 0) {
		return 1;
	}
	forbidden_key = sqlparser_test_forbidden_query_graph_key(graph);
	if (forbidden_key != NULL) {
		return sqlparser_test_fail_case_field(case_id, case_name, "query_graph contains forbidden key", forbidden_key);
	}
	null_key = sqlparser_test_null_query_graph_key(graph);
	if (null_key != NULL) {
		return sqlparser_test_fail_case_field(case_id, case_name, "query_graph contains null field", null_key);
	}
	empty_array_key = sqlparser_test_empty_query_graph_array_key(graph);
	if (empty_array_key != NULL) {
		return sqlparser_test_fail_case_field(case_id, case_name, "query_graph contains empty array", empty_array_key);
	}
	return 0;
}

static int sqlparser_test_verify_view_shape(
	const char *case_id,
	const char *case_name,
	const char *view_json,
	json_t *expect_root)
{
	json_error_t error;
	json_t *expected_session;
	json_t *root;
	json_t *statements;
	json_t *statement;
	size_t index;

	root = json_loads(view_json, 0, &error);
	if (root == NULL) {
		return sqlparser_test_fail_case(case_id, case_name, "view JSON should decode");
	}
	statements = json_object_get(root, "statements");
	if (!json_is_array(statements)) {
		json_decref(root);
		return sqlparser_test_fail_case(case_id, case_name, "view JSON statements should be an array");
	}
	json_array_foreach(statements, index, statement)
	{
		if (sqlparser_test_verify_query_graph_shape(case_id, case_name, statement) != 0) {
			json_decref(root);
			return 1;
		}
	}
	expected_session = expect_root != NULL ?
		json_object_get(expect_root, "session") :
		NULL;
	if (expected_session != NULL &&
	    (!json_is_array(expected_session) ||
	     json_array_size(expected_session) != json_array_size(statements))) {
		json_decref(root);
		return sqlparser_test_fail_case(
			case_id,
			case_name,
			"session expectation must match the statements array");
	}
	json_array_foreach(statements, index, statement)
	{
		json_t *actual;
		json_t *expected;

		actual = json_object_get(
			json_object_get(statement, "query_graph"),
			"session");
		expected = expected_session != NULL ?
			json_array_get(expected_session, index) :
			NULL;
		if (expected == NULL || json_is_null(expected)) {
			if (actual != NULL) {
				json_decref(root);
				return sqlparser_test_fail_case(
					case_id,
					case_name,
					"unexpected session graph");
			}
			continue;
		}
		if (!json_is_object(expected)) {
			json_decref(root);
			return sqlparser_test_fail_case(
				case_id,
				case_name,
				"session expectation item must be an object or null");
		}
		if (actual == NULL || !json_equal(expected, actual)) {
			json_decref(root);
			return sqlparser_test_fail_case(
				case_id,
				case_name,
				"session graph mismatch");
		}
	}
	json_decref(root);
	return 0;
}

static int sqlparser_test_verify_view_expectations(
	const char *case_id,
	const char *case_name,
	const char *view_json,
	json_t *expect_root)
{
	if (expect_root == NULL) {
		return sqlparser_test_verify_view_shape(case_id, case_name, view_json, NULL);
	}
	if (sqlparser_test_text_contains_expected(
		    case_id,
		    case_name,
		    view_json,
		    "view_contains",
		    json_object_get(expect_root, "view_contains")) != 0 ||
	    sqlparser_test_text_not_contains_expected(
		    case_id,
		    case_name,
		    view_json,
		    "view_not_contains",
		    json_object_get(expect_root, "view_not_contains")) != 0) {
		return 1;
	}
	return sqlparser_test_verify_view_shape(case_id, case_name, view_json, expect_root);
}

#endif
