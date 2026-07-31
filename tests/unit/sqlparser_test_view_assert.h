#ifndef SQLPARSER_TEST_VIEW_ASSERT_H
#define SQLPARSER_TEST_VIEW_ASSERT_H

#include <ctype.h>
#include <jansson.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sqlparser_internal.h"

sqlparser_status_t sqlparser_get_statement_node(
	sqlparser_handle_t *handle,
	size_t statement_index,
	PgQuery__Node **out_statement,
	sqlparser_error_t *out_error);

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

static int sqlparser_test_verify_statement_types(
	const char *case_id,
	const char *case_name,
	const sqlparser_handle_t *handle,
	json_t *expected_array)
{
	json_t *value;
	size_t expected_count;
	size_t expected_index;
	size_t statement_count;
	size_t statement_index;
	int exact;

	if (expected_array == NULL) {
		return 0;
	}
	if (handle == NULL || !json_is_array(expected_array)) {
		return sqlparser_test_fail_case(
			case_id,
			case_name,
			"statement_types must be an array");
	}
	expected_count = json_array_size(expected_array);
	statement_count = sqlparser_statement_count(handle);
	if (expected_count > statement_count) {
		return sqlparser_test_fail_case(
			case_id,
			case_name,
			"statement_types contains more items than the parsed statements");
	}
	exact = expected_count == statement_count;
	statement_index = 0U;
	json_array_foreach(expected_array, expected_index, value)
	{
		const char *expected;
		int found;

		expected = sqlparser_test_string_value(value);
		if (expected == NULL) {
			return sqlparser_test_fail_case(
				case_id,
				case_name,
				"statement_types value must be a string");
		}
		found = 0;
		if (exact) {
			const char *actual;
			sqlparser_error_t error;

			actual = NULL;
			memset(&error, 0, sizeof(error));
			if (sqlparser_statement_node_name(
				    handle,
				    expected_index,
				    &actual,
				    &error) != SQLPARSER_STATUS_OK ||
			    actual == NULL ||
			    strcmp(actual, expected) != 0) {
				return sqlparser_test_fail_case_field(
					case_id,
					case_name,
					"statement_types",
					expected);
			}
			continue;
		}
		for (; statement_index < statement_count;
		     statement_index++) {
			const char *actual;
			sqlparser_error_t error;

			actual = NULL;
			memset(&error, 0, sizeof(error));
			if (sqlparser_statement_node_name(
				    handle,
				    statement_index,
				    &actual,
				    &error) != SQLPARSER_STATUS_OK ||
			    actual == NULL) {
				return sqlparser_test_fail_case(
					case_id,
					case_name,
					"statement type lookup failed");
			}
			if (strcmp(actual, expected) == 0) {
				statement_index++;
				found = 1;
				break;
			}
		}
		if (!found) {
			return sqlparser_test_fail_case_field(
				case_id,
				case_name,
				"statement_types",
				expected);
		}
	}
	return 0;
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

static int sqlparser_test_verify_dml_cell_array_shape(
	const char *case_id,
	const char *case_name,
	json_t *rows)
{
	json_t *cell;
	size_t index;

	if (rows == NULL) {
		return 0;
	}
	if (!json_is_array(rows) || json_array_size(rows) == 0U) {
		return sqlparser_test_fail_case(
			case_id,
			case_name,
			"DML rows must be a non-empty array");
	}
	json_array_foreach(rows, index, cell)
	{
		const char *kind;
		json_t *expression_sql;

		if (!json_is_object(cell) ||
		    !json_is_integer(json_object_get(cell, "row")) ||
		    json_integer_value(json_object_get(cell, "row")) < 0 ||
		    !json_is_integer(json_object_get(cell, "column")) ||
		    json_integer_value(json_object_get(cell, "column")) < 0) {
			return sqlparser_test_fail_case(
				case_id,
				case_name,
				"DML cell coordinates are invalid");
		}
		kind = sqlparser_test_string_value(
			json_object_get(cell, "kind"));
		if (kind == NULL) {
			return sqlparser_test_fail_case(
				case_id,
				case_name,
				"DML cell kind is missing");
		}
		expression_sql = json_object_get(cell, "expression_sql");
		if (strcmp(kind, "expression") == 0) {
			if (!json_is_string(expression_sql) ||
			    json_string_length(expression_sql) == 0U) {
				return sqlparser_test_fail_case(
					case_id,
					case_name,
					"DML expression cell source SQL is missing");
			}
		} else if (expression_sql != NULL) {
			return sqlparser_test_fail_case(
				case_id,
				case_name,
				"non-expression DML cell exposes expression_sql");
		}
	}
	return 0;
}

static int sqlparser_test_verify_dml_shape(
	const char *case_id,
	const char *case_name,
	json_t *dml)
{
	json_t *branches;
	json_t *children;
	json_t *item;
	size_t index;

	if (dml == NULL) {
		return 0;
	}
	if (!json_is_object(dml) ||
	    !json_is_string(json_object_get(dml, "kind"))) {
		return sqlparser_test_fail_case(
			case_id,
			case_name,
			"DML graph object is invalid");
	}
	if (sqlparser_test_verify_dml_cell_array_shape(
		    case_id,
		    case_name,
		    json_object_get(dml, "rows")) != 0) {
		return 1;
	}
	branches = json_object_get(dml, "branches");
	if (branches != NULL) {
		if (!json_is_array(branches) ||
		    json_array_size(branches) == 0U) {
			return sqlparser_test_fail_case(
				case_id,
				case_name,
				"DML branches are invalid");
		}
		json_array_foreach(branches, index, item)
		{
			if (!json_is_object(item) ||
			    sqlparser_test_verify_dml_cell_array_shape(
				    case_id,
				    case_name,
				    json_object_get(item, "rows")) != 0) {
				return 1;
			}
		}
	}
	children = json_object_get(dml, "children");
	if (children != NULL) {
		if (!json_is_array(children) ||
		    json_array_size(children) == 0U) {
			return sqlparser_test_fail_case(
				case_id,
				case_name,
				"DML children are invalid");
		}
		json_array_foreach(children, index, item)
		{
			if (sqlparser_test_verify_dml_shape(
				    case_id,
				    case_name,
				    item) != 0) {
				return 1;
			}
		}
	}
	return 0;
}

static int sqlparser_test_is_bind_mixed_case(const char *case_id)
{
	const char *suffix;

	if (case_id == NULL) {
		return 0;
	}
	suffix = strstr(case_id, "-BM");
	if (suffix == NULL || !isdigit((unsigned char)suffix[3])) {
		return 0;
	}
	for (suffix += 3; *suffix != '\0'; suffix++) {
		if (!isdigit((unsigned char)*suffix)) {
			return 0;
		}
	}
	return 1;
}

static int sqlparser_test_bind_mixed_graph_fields_absent(
	json_t *graph)
{
	return json_is_object(graph) &&
		json_object_get(graph, "fields") == NULL;
}

static int sqlparser_test_verify_bind_mixed_graph_field_gate(void)
{
	json_t *fields;
	json_t *graph;

	graph = json_object();
	fields = json_array();
	if (graph == NULL || fields == NULL ||
	    !sqlparser_test_bind_mixed_graph_fields_absent(graph)) {
		json_decref(graph);
		json_decref(fields);
		fprintf(
			stderr,
			"FAIL: bind-mixed graph field gate self-test setup failed\n");
		return 1;
	}
	if (json_object_set(graph, "fields", fields) != 0) {
		json_decref(graph);
		json_decref(fields);
		fprintf(
			stderr,
			"FAIL: bind-mixed graph field gate self-test setup failed\n");
		return 1;
	}
	json_decref(fields);
	if (sqlparser_test_bind_mixed_graph_fields_absent(graph)) {
		json_decref(graph);
		fprintf(
			stderr,
			"FAIL: bind-mixed graph field gate accepted fields\n");
		return 1;
	}
	json_decref(graph);
	return 0;
}

static int sqlparser_test_nonempty_string(json_t *value)
{
	return json_is_string(value) &&
		json_string_length(value) > 0U;
}

static int sqlparser_test_nonempty_string_array(json_t *value)
{
	json_t *item;
	size_t index;

	if (!json_is_array(value) ||
	    json_array_size(value) == 0U) {
		return 0;
	}
	json_array_foreach(value, index, item)
	{
		if (!sqlparser_test_nonempty_string(item)) {
			return 0;
		}
	}
	return 1;
}

static int sqlparser_test_nonempty_string_or_array(
	json_t *value)
{
	return sqlparser_test_nonempty_string(value) ||
		sqlparser_test_nonempty_string_array(value);
}

static json_t *sqlparser_test_bind_mixed_expected_cell(
	const char *fragment)
{
	json_error_t error;
	char *object_text;
	json_t *cell;
	size_t fragment_length;

	if (fragment == NULL) {
		return NULL;
	}
	fragment_length = strlen(fragment);
	if (fragment_length > (size_t)-1 - 3U) {
		return NULL;
	}
	object_text = (char *)malloc(fragment_length + 3U);
	if (object_text == NULL) {
		return NULL;
	}
	object_text[0] = '{';
	memcpy(object_text + 1U, fragment, fragment_length);
	object_text[fragment_length + 1U] = '}';
	object_text[fragment_length + 2U] = '\0';
	cell = json_loads(object_text, 0, &error);
	free(object_text);
	return json_is_object(cell) ? cell :
		(json_decref(cell), (json_t *)NULL);
}

static int sqlparser_test_bind_mixed_expected_cell_is_valid(
	json_t *cell)
{
	char expected_selector[128];
	const char *kind;
	const char *literal_kind;
	const char *selector;
	json_int_t column;
	json_int_t row;
	json_t *literal;
	int length;

	if (!json_is_object(cell) ||
	    !json_is_integer(json_object_get(cell, "row")) ||
	    !json_is_integer(json_object_get(cell, "column")) ||
	    !json_is_integer(json_object_get(cell, "bind_kind"))) {
		return 0;
	}
	row = json_integer_value(json_object_get(cell, "row"));
	column = json_integer_value(json_object_get(cell, "column"));
	kind = sqlparser_test_string_value(json_object_get(cell, "kind"));
	selector = sqlparser_test_string_value(
		json_object_get(cell, "selector"));
	if (row < 0 || column < 0 || kind == NULL || selector == NULL) {
		return 0;
	}
	length = snprintf(
		expected_selector,
		sizeof(expected_selector),
		"stmt[0].insert_cell[%lld][%lld]",
		(long long)row,
		(long long)column);
	if (length < 0 ||
	    (size_t)length >= sizeof(expected_selector) ||
	    strcmp(selector, expected_selector) != 0) {
		return 0;
	}
	if (strcmp(kind, "bind") == 0) {
		json_int_t bind_kind;

		bind_kind = json_integer_value(
			json_object_get(cell, "bind_kind"));
		return json_object_size(cell) == 8U &&
			(bind_kind == SQLPARSER_BIND_KIND_POSITIONAL ||
			 bind_kind == SQLPARSER_BIND_KIND_NAMED) &&
			sqlparser_test_nonempty_string(
				json_object_get(cell, "bind_key")) &&
			sqlparser_test_nonempty_string(
				json_object_get(cell, "bind_sql")) &&
			json_is_integer(
				json_object_get(cell, "bind_position")) &&
			json_integer_value(
				json_object_get(cell, "bind_position")) > 0;
	}
	if (json_integer_value(
		    json_object_get(cell, "bind_kind")) !=
	    SQLPARSER_BIND_KIND_NONE) {
		return 0;
	}
	if (strcmp(kind, "expression") == 0) {
		return json_object_size(cell) == 6U &&
			sqlparser_test_nonempty_string(
				json_object_get(cell, "expression_sql"));
	}
	if (strcmp(kind, "default") == 0) {
		return json_object_size(cell) == 5U;
	}
	if (strcmp(kind, "literal") != 0 ||
	    json_object_size(cell) != 6U) {
		return 0;
	}
	literal = json_object_get(cell, "literal");
	literal_kind = json_is_object(literal) ?
		sqlparser_test_string_value(
			json_object_get(literal, "kind")) :
		NULL;
	if (literal_kind == NULL) {
		return 0;
	}
	if (strcmp(literal_kind, "null") == 0) {
		return json_object_size(literal) == 1U;
	}
	return strcmp(literal_kind, "string") == 0 &&
		json_object_size(literal) == 2U &&
		json_is_string(
			json_object_get(literal, "string_value"));
}

static int sqlparser_test_expectation_key_allowed(
	const char *key)
{
	static const char *const keys[] = {
		"error_code",
		"error_message_contains",
		"ok",
		"session",
		"statement_count",
		"statement_types",
		"view_contains",
		"view_merge_exact",
		"view_not_contains",
		"view_target_columns",
		"view_target_relation"
	};
	size_t index;

	for (index = 0U;
	     index < sizeof(keys) / sizeof(keys[0]);
	     index++) {
		if (strcmp(key, keys[index]) == 0) {
			return 1;
		}
	}
	return 0;
}

static int sqlparser_test_schema_failure(
	const char *case_id,
	const char *case_name,
	int report_errors,
	const char *message,
	const char *field)
{
	if (!report_errors) {
		return 1;
	}
	if (field != NULL) {
		return sqlparser_test_fail_case_field(
			case_id,
			case_name,
			message,
			field);
	}
	return sqlparser_test_fail_case(
		case_id,
		case_name,
		message);
}

static int sqlparser_test_validate_expectation_schema(
	const char *case_id,
	const char *case_name,
	json_t *expect_root,
	int report_errors,
	int *out_expected_ok)
{
	static const char *const success_fields[] = {
		"session",
		"statement_count",
		"statement_types",
		"view_contains",
		"view_merge_exact",
		"view_not_contains",
		"view_target_columns",
		"view_target_relation"
	};
	const char *key;
	json_t *item;
	json_t *ok;
	json_t *session;
	json_t *statement_count;
	json_t *value;
	json_int_t count;
	size_t index;
	int bind_mixed;
	int expected_ok;

	if (!json_is_object(expect_root) ||
	    out_expected_ok == NULL) {
		return sqlparser_test_schema_failure(
			case_id,
			case_name,
			report_errors,
			"case expectation object is invalid",
			NULL);
	}
	json_object_foreach(expect_root, key, value)
	{
		(void)value;
		if (!sqlparser_test_expectation_key_allowed(key)) {
			return sqlparser_test_schema_failure(
				case_id,
				case_name,
				report_errors,
				"unknown expectation field",
				key);
		}
	}
	ok = json_object_get(expect_root, "ok");
	if (ok != NULL && !json_is_boolean(ok)) {
		return sqlparser_test_schema_failure(
			case_id,
			case_name,
			report_errors,
			"expect.ok must be boolean when present",
			NULL);
	}
	expected_ok = ok == NULL || json_is_true(ok);
	if (!expected_ok) {
		for (index = 0U;
		     index <
			     sizeof(success_fields) /
				     sizeof(success_fields[0]);
		     index++) {
			if (json_object_get(
				    expect_root,
				    success_fields[index]) != NULL) {
				return sqlparser_test_schema_failure(
					case_id,
					case_name,
					report_errors,
					"failure expectation contains success/View field",
					success_fields[index]);
			}
		}
		value = json_object_get(expect_root, "error_code");
		if (value != NULL &&
		    (!json_is_integer(value) ||
		     json_integer_value(value) <
			     SQLPARSER_STATUS_INVALID_ARGUMENT ||
		     json_integer_value(value) >
			     SQLPARSER_STATUS_RESOURCE_LIMIT)) {
			return sqlparser_test_schema_failure(
				case_id,
				case_name,
				report_errors,
				"expect.error_code must be a valid non-OK status",
				NULL);
		}
		value = json_object_get(
			expect_root,
			"error_message_contains");
		if (value != NULL &&
		    !sqlparser_test_nonempty_string(value)) {
			return sqlparser_test_schema_failure(
				case_id,
				case_name,
				report_errors,
				"expect.error_message_contains must be a non-empty string",
				NULL);
		}
		*out_expected_ok = 0;
		return 0;
	}
	if (json_object_get(expect_root, "error_code") != NULL ||
	    json_object_get(
		    expect_root,
		    "error_message_contains") != NULL) {
		return sqlparser_test_schema_failure(
			case_id,
			case_name,
			report_errors,
			"success expectation must not contain error fields",
			NULL);
	}
	statement_count = json_object_get(
		expect_root,
		"statement_count");
	if (!json_is_integer(statement_count) ||
	    json_integer_value(statement_count) <= 0) {
		return sqlparser_test_schema_failure(
			case_id,
			case_name,
			report_errors,
			"success expect.statement_count must be a positive integer",
			NULL);
	}
	count = json_integer_value(statement_count);
	value = json_object_get(expect_root, "statement_types");
	if (value != NULL &&
	    (!sqlparser_test_nonempty_string_array(value) ||
	     json_array_size(value) > (size_t)count)) {
		return sqlparser_test_schema_failure(
			case_id,
			case_name,
			report_errors,
			"expect.statement_types must be a non-empty string array no longer than statement_count",
			NULL);
	}
	session = json_object_get(expect_root, "session");
	if (session != NULL) {
		if (!json_is_array(session) ||
		    json_array_size(session) != (size_t)count) {
			return sqlparser_test_schema_failure(
				case_id,
				case_name,
				report_errors,
				"expect.session must match statement_count",
				NULL);
		}
		json_array_foreach(session, index, item)
		{
			if (!json_is_object(item) &&
			    !json_is_null(item)) {
				return sqlparser_test_schema_failure(
					case_id,
					case_name,
					report_errors,
					"expect.session items must be objects or null",
					NULL);
			}
		}
	}
	value = json_object_get(expect_root, "view_contains");
	if (value != NULL &&
	    !sqlparser_test_nonempty_string_or_array(value)) {
		return sqlparser_test_schema_failure(
			case_id,
			case_name,
			report_errors,
			"expect.view_contains must be a non-empty string or string array",
			NULL);
	}
	value = json_object_get(expect_root, "view_not_contains");
	if (value != NULL &&
	    !sqlparser_test_nonempty_string_or_array(value)) {
		return sqlparser_test_schema_failure(
			case_id,
			case_name,
			report_errors,
			"expect.view_not_contains must be a non-empty string or string array",
			NULL);
	}
	value = json_object_get(expect_root, "view_merge_exact");
	if (value != NULL &&
	    (!json_is_object(value) ||
	     json_object_size(value) == 0U)) {
		return sqlparser_test_schema_failure(
			case_id,
			case_name,
			report_errors,
			"expect.view_merge_exact must be a non-empty object",
			NULL);
	}
	value = json_object_get(
		expect_root,
		"view_target_relation");
	if (value != NULL) {
		const char *relation_key;
		json_t *relation_value;

		if (!json_is_object(value) ||
		    json_object_size(value) == 0U ||
		    !sqlparser_test_nonempty_string(
			    json_object_get(value, "table"))) {
			return sqlparser_test_schema_failure(
				case_id,
				case_name,
				report_errors,
				"expect.view_target_relation must contain a non-empty table",
				NULL);
		}
		json_object_foreach(value, relation_key, relation_value)
		{
			if ((strcmp(relation_key, "database") != 0 &&
			     strcmp(relation_key, "schema") != 0 &&
			     strcmp(relation_key, "table") != 0) ||
			    !sqlparser_test_nonempty_string(
				    relation_value)) {
				return sqlparser_test_schema_failure(
					case_id,
					case_name,
					report_errors,
					"invalid view_target_relation field",
					relation_key);
			}
		}
	}
	value = json_object_get(
		expect_root,
		"view_target_columns");
	if (value != NULL &&
	    !sqlparser_test_nonempty_string_array(value)) {
		return sqlparser_test_schema_failure(
			case_id,
			case_name,
			report_errors,
			"expect.view_target_columns must be a non-empty string array",
			NULL);
	}
	bind_mixed = sqlparser_test_is_bind_mixed_case(case_id);
	if (bind_mixed) {
		json_t *expected_cell;
		json_t *expected_fragments;

		expected_fragments = json_object_get(
			expect_root,
			"view_contains");
		if (json_object_get(
			    expect_root,
			    "view_target_relation") == NULL ||
		    json_object_get(
			    expect_root,
			    "view_target_columns") == NULL ||
		    !sqlparser_test_nonempty_string_array(
			    json_object_get(
				    expect_root,
				    "view_contains"))) {
			return sqlparser_test_schema_failure(
				case_id,
				case_name,
				report_errors,
				"bind-mixed case requires target relation, target columns, and a view_contains string array",
				NULL);
		}
		json_array_foreach(expected_fragments, index, item)
		{
			expected_cell =
				sqlparser_test_bind_mixed_expected_cell(
					json_string_value(item));
			if (!sqlparser_test_bind_mixed_expected_cell_is_valid(
				    expected_cell)) {
				json_decref(expected_cell);
				return sqlparser_test_schema_failure(
					case_id,
					case_name,
					report_errors,
					"bind-mixed case contains an invalid exact cell expectation",
					NULL);
			}
			json_decref(expected_cell);
		}
	} else if (json_object_get(
			   expect_root,
			   "view_target_relation") != NULL ||
		   json_object_get(
			   expect_root,
			   "view_target_columns") != NULL) {
		return sqlparser_test_schema_failure(
			case_id,
			case_name,
			report_errors,
			"non-bind-mixed case must not contain bind-mixed target fields",
			NULL);
	}
	*out_expected_ok = 1;
	return 0;
}

static int sqlparser_test_validate_case_schema_internal(
	json_t *case_item,
	int require_id,
	int allow_dialect,
	int allow_official_item,
	int report_errors,
	int *out_expected_ok)
{
	const char *case_id;
	const char *case_name;
	const char *key;
	json_t *expect_root;
	json_t *value;

	if (!json_is_object(case_item)) {
		return sqlparser_test_schema_failure(
			NULL,
			NULL,
			report_errors,
			"case item must be an object",
			NULL);
	}
	case_id = sqlparser_test_string_value(
		json_object_get(case_item, "id"));
	case_name = sqlparser_test_string_value(
		json_object_get(case_item, "name"));
	json_object_foreach(case_item, key, value)
	{
		(void)value;
		if (strcmp(key, "id") != 0 &&
		    strcmp(key, "name") != 0 &&
		    strcmp(key, "sql") != 0 &&
		    strcmp(key, "expect") != 0 &&
		    !(allow_dialect &&
		      strcmp(key, "dialect") == 0) &&
		    !(allow_official_item &&
		      strcmp(key, "official_item") == 0)) {
			return sqlparser_test_schema_failure(
				case_id,
				case_name,
				report_errors,
				"unknown case item field",
				key);
		}
	}
	value = json_object_get(case_item, "id");
	if ((require_id &&
	     !sqlparser_test_nonempty_string(value)) ||
	    (!require_id &&
	     value != NULL &&
	     !sqlparser_test_nonempty_string(value))) {
		return sqlparser_test_schema_failure(
			case_id,
			case_name,
			report_errors,
			"case id must be a non-empty string",
			NULL);
	}
	if (!sqlparser_test_nonempty_string(
		    json_object_get(case_item, "name")) ||
	    !sqlparser_test_nonempty_string(
		    json_object_get(case_item, "sql"))) {
		return sqlparser_test_schema_failure(
			case_id,
			case_name,
			report_errors,
			"case name and sql must be non-empty strings",
			NULL);
	}
	value = json_object_get(case_item, "dialect");
	if (value != NULL &&
	    (!allow_dialect ||
	     !sqlparser_test_nonempty_string(value))) {
		return sqlparser_test_schema_failure(
			case_id,
			case_name,
			report_errors,
			"case dialect is not allowed or is empty",
			NULL);
	}
	value = json_object_get(case_item, "official_item");
	if (value != NULL &&
	    (!allow_official_item ||
	     !sqlparser_test_nonempty_string(value))) {
		return sqlparser_test_schema_failure(
			case_id,
			case_name,
			report_errors,
			"case official_item is not allowed or is empty",
			NULL);
	}
	expect_root = json_object_get(case_item, "expect");
	return sqlparser_test_validate_expectation_schema(
		case_id,
		case_name,
		expect_root,
		report_errors,
		out_expected_ok);
}

static int sqlparser_test_validate_case_schema(
	json_t *case_item,
	int require_id,
	int allow_dialect,
	int allow_official_item,
	int *out_expected_ok)
{
	return sqlparser_test_validate_case_schema_internal(
		case_item,
		require_id,
		allow_dialect,
		allow_official_item,
		1,
		out_expected_ok);
}

static int sqlparser_test_case_schema_json_result(
	const char *json,
	int require_id,
	int allow_dialect,
	int allow_official_item,
	int expected_result)
{
	json_error_t error;
	json_t *item;
	int expected_ok;
	int status;

	memset(&error, 0, sizeof(error));
	item = json_loads(json, 0, &error);
	if (item == NULL) {
		fprintf(
			stderr,
			"FAIL: case schema self-test JSON is invalid: %s\n",
			error.text);
		return 1;
	}
	expected_ok = -1;
	status = sqlparser_test_validate_case_schema_internal(
		item,
		require_id,
		allow_dialect,
		allow_official_item,
		0,
		&expected_ok);
	json_decref(item);
	if ((expected_result < 0 && status == 0) ||
	    (expected_result >= 0 &&
	     (status != 0 || expected_ok != expected_result))) {
		fprintf(
			stderr,
			"FAIL: case schema self-test result mismatch\n");
		return 1;
	}
	return 0;
}

static int sqlparser_test_verify_case_schema_gate(
	int require_id,
	int allow_dialect,
	int allow_official_item)
{
	static const struct {
		const char *json;
		int expected_result;
	} cases[] = {
		{"{\"id\":\"T\",\"name\":\"n\",\"sql\":\"SELECT 1\",\"expect\":{\"statement_count\":1}}", 1},
		{"{\"id\":\"T\",\"name\":\"n\",\"sql\":\"SELECT 1\",\"expect\":{\"ok\":true,\"statement_count\":2,\"statement_types\":[\"SelectStmt\"],\"session\":[{},null],\"view_contains\":\"x\",\"view_not_contains\":[\"y\"],\"view_merge_exact\":{\"x\":1}}}", 1},
		{"{\"id\":\"T-BM001\",\"name\":\"n\",\"sql\":\"INSERT INTO t(c) VALUES (?)\",\"expect\":{\"statement_count\":1,\"view_target_relation\":{\"table\":\"t\"},\"view_target_columns\":[\"c\"],\"view_contains\":[\"\\\"row\\\":0,\\\"column\\\":0,\\\"kind\\\":\\\"bind\\\",\\\"bind_key\\\":\\\"1\\\",\\\"bind_kind\\\":1,\\\"bind_sql\\\":\\\"?\\\",\\\"bind_position\\\":1,\\\"selector\\\":\\\"stmt[0].insert_cell[0][0]\\\"\"]}}", 1},
		{"{\"id\":\"T\",\"name\":\"n\",\"sql\":\"bad\",\"expect\":{\"ok\":false}}", 0},
		{"{\"id\":\"T\",\"name\":\"n\",\"sql\":\"bad\",\"expect\":{\"ok\":false,\"error_code\":6,\"error_message_contains\":\"x\"}}", 0},
		{"[]", -1},
		{"{\"id\":\"T\",\"name\":\"n\",\"sql\":\"SELECT 1\",\"extra\":1,\"expect\":{\"statement_count\":1}}", -1},
		{"{\"id\":\"\",\"name\":\"n\",\"sql\":\"SELECT 1\",\"expect\":{\"statement_count\":1}}", -1},
		{"{\"id\":\"T\",\"name\":\"\",\"sql\":\"SELECT 1\",\"expect\":{\"statement_count\":1}}", -1},
		{"{\"id\":\"T\",\"name\":\"n\",\"sql\":\"\",\"expect\":{\"statement_count\":1}}", -1},
		{"{\"id\":\"T\",\"name\":\"n\",\"sql\":\"SELECT 1\"}", -1},
		{"{\"id\":\"T\",\"name\":\"n\",\"sql\":\"SELECT 1\",\"expect\":{\"statement_count\":1,\"extra\":1}}", -1},
		{"{\"id\":\"T\",\"name\":\"n\",\"sql\":\"SELECT 1\",\"expect\":{\"ok\":1,\"statement_count\":1}}", -1},
		{"{\"id\":\"T\",\"name\":\"n\",\"sql\":\"SELECT 1\",\"expect\":{}}", -1},
		{"{\"id\":\"T\",\"name\":\"n\",\"sql\":\"SELECT 1\",\"expect\":{\"statement_count\":0}}", -1},
		{"{\"id\":\"T\",\"name\":\"n\",\"sql\":\"SELECT 1\",\"expect\":{\"statement_count\":\"1\"}}", -1},
		{"{\"id\":\"T\",\"name\":\"n\",\"sql\":\"SELECT 1\",\"expect\":{\"statement_count\":1,\"error_code\":3}}", -1},
		{"{\"id\":\"T\",\"name\":\"n\",\"sql\":\"bad\",\"expect\":{\"ok\":false,\"statement_count\":1}}", -1},
		{"{\"id\":\"T\",\"name\":\"n\",\"sql\":\"bad\",\"expect\":{\"ok\":false,\"error_code\":0}}", -1},
		{"{\"id\":\"T\",\"name\":\"n\",\"sql\":\"bad\",\"expect\":{\"ok\":false,\"error_code\":7}}", -1},
		{"{\"id\":\"T\",\"name\":\"n\",\"sql\":\"bad\",\"expect\":{\"ok\":false,\"error_code\":\"3\"}}", -1},
		{"{\"id\":\"T\",\"name\":\"n\",\"sql\":\"bad\",\"expect\":{\"ok\":false,\"error_message_contains\":\"\"}}", -1},
		{"{\"id\":\"T\",\"name\":\"n\",\"sql\":\"SELECT 1\",\"expect\":{\"statement_count\":1,\"statement_types\":[]}}", -1},
		{"{\"id\":\"T\",\"name\":\"n\",\"sql\":\"SELECT 1\",\"expect\":{\"statement_count\":1,\"statement_types\":[\"\"]}}", -1},
		{"{\"id\":\"T\",\"name\":\"n\",\"sql\":\"SELECT 1\",\"expect\":{\"statement_count\":1,\"statement_types\":[\"A\",\"B\"]}}", -1},
		{"{\"id\":\"T\",\"name\":\"n\",\"sql\":\"SELECT 1\",\"expect\":{\"statement_count\":1,\"session\":[]}}", -1},
		{"{\"id\":\"T\",\"name\":\"n\",\"sql\":\"SELECT 1\",\"expect\":{\"statement_count\":1,\"session\":[1]}}", -1},
		{"{\"id\":\"T\",\"name\":\"n\",\"sql\":\"SELECT 1\",\"expect\":{\"statement_count\":1,\"view_contains\":\"\"}}", -1},
		{"{\"id\":\"T\",\"name\":\"n\",\"sql\":\"SELECT 1\",\"expect\":{\"statement_count\":1,\"view_contains\":[]}}", -1},
		{"{\"id\":\"T\",\"name\":\"n\",\"sql\":\"SELECT 1\",\"expect\":{\"statement_count\":1,\"view_contains\":1}}", -1},
		{"{\"id\":\"T\",\"name\":\"n\",\"sql\":\"SELECT 1\",\"expect\":{\"statement_count\":1,\"view_not_contains\":[\"\"]}}", -1},
		{"{\"id\":\"T\",\"name\":\"n\",\"sql\":\"SELECT 1\",\"expect\":{\"statement_count\":1,\"view_merge_exact\":{}}}", -1},
		{"{\"id\":\"T\",\"name\":\"n\",\"sql\":\"SELECT 1\",\"expect\":{\"statement_count\":1,\"view_target_relation\":{\"table\":\"t\"},\"view_target_columns\":[\"c\"]}}", -1},
		{"{\"id\":\"T-BM001\",\"name\":\"n\",\"sql\":\"INSERT INTO t(c) VALUES (?)\",\"expect\":{\"statement_count\":1,\"view_contains\":\"x\"}}", -1},
		{"{\"id\":\"T-BM001\",\"name\":\"n\",\"sql\":\"INSERT INTO t(c) VALUES (?)\",\"expect\":{\"statement_count\":1,\"view_target_relation\":{\"table\":\"t\"},\"view_target_columns\":[\"c\"],\"view_contains\":\"x\"}}", -1},
		{"{\"id\":\"T-BM001\",\"name\":\"n\",\"sql\":\"INSERT INTO t(c) VALUES (?)\",\"expect\":{\"statement_count\":1,\"view_target_relation\":{\"table\":\"t\"},\"view_target_columns\":[],\"view_contains\":\"x\"}}", -1},
		{"{\"id\":\"T-BM001\",\"name\":\"n\",\"sql\":\"INSERT INTO t(c) VALUES (?)\",\"expect\":{\"statement_count\":1,\"view_target_relation\":{\"table\":\"t\",\"alias\":\"t\"},\"view_target_columns\":[\"c\"],\"view_contains\":\"x\"}}", -1},
		{"{\"id\":\"T-BM001\",\"name\":\"n\",\"sql\":\"INSERT INTO t(c) VALUES (CURRENT_TIMESTAMP)\",\"expect\":{\"statement_count\":1,\"view_target_relation\":{\"table\":\"t\"},\"view_target_columns\":[\"c\"],\"view_contains\":[\"\\\"row\\\":0,\\\"column\\\":0,\\\"kind\\\":\\\"expression\\\",\\\"bind_kind\\\":0,\\\"selector\\\":\\\"stmt[0].insert_cell[0][0]\\\"\"]}}", -1},
		{"{\"id\":\"T-BM001\",\"name\":\"n\",\"sql\":\"INSERT INTO t(c) VALUES (?)\",\"expect\":{\"statement_count\":1,\"view_target_relation\":{\"table\":\"t\"},\"view_target_columns\":[\"c\"],\"view_contains\":[\"\\\"row\\\":0,\\\"column\\\":0,\\\"kind\\\":\\\"bind\\\",\\\"bind_key\\\":\\\"1\\\",\\\"bind_kind\\\":1,\\\"bind_sql\\\":\\\"?\\\",\\\"bind_position\\\":1,\\\"selector\\\":\\\"stmt[0].insert_cell[1][0]\\\"\"]}}", -1},
		{"{\"id\":\"T-BM001\",\"name\":\"n\",\"sql\":\"INSERT INTO t(c) VALUES (?)\",\"expect\":{\"statement_count\":1,\"view_target_relation\":{\"table\":\"t\"},\"view_target_columns\":[\"c\"],\"view_contains\":[\"\\\"row\\\":-1,\\\"column\\\":0,\\\"kind\\\":\\\"bind\\\",\\\"bind_key\\\":\\\"1\\\",\\\"bind_kind\\\":1,\\\"bind_sql\\\":\\\"?\\\",\\\"bind_position\\\":1,\\\"selector\\\":\\\"stmt[0].insert_cell[-1][0]\\\"\"]}}", -1}
	};
	size_t index;

	for (index = 0U;
	     index < sizeof(cases) / sizeof(cases[0]);
	     index++) {
		if (sqlparser_test_case_schema_json_result(
			    cases[index].json,
			    require_id,
			    allow_dialect,
			    allow_official_item,
			    cases[index].expected_result) != 0) {
			return 1;
		}
	}
	if (sqlparser_test_case_schema_json_result(
		    "{\"name\":\"n\",\"sql\":\"SELECT 1\",\"expect\":{\"statement_count\":1}}",
		    require_id,
		    allow_dialect,
		    allow_official_item,
		    require_id ? -1 : 1) != 0 ||
	    sqlparser_test_case_schema_json_result(
		    "{\"id\":\"T\",\"name\":\"n\",\"sql\":\"SELECT 1\",\"dialect\":\"postgresql\",\"expect\":{\"statement_count\":1}}",
		    require_id,
		    allow_dialect,
		    allow_official_item,
		    allow_dialect ? 1 : -1) != 0 ||
	    sqlparser_test_case_schema_json_result(
		    "{\"id\":\"T\",\"name\":\"n\",\"sql\":\"SELECT 1\",\"official_item\":\"source\",\"expect\":{\"statement_count\":1}}",
		    require_id,
		    allow_dialect,
		    allow_official_item,
		    allow_official_item ? 1 : -1) != 0 ||
	    sqlparser_test_case_schema_json_result(
		    "{\"id\":\"T\",\"name\":\"n\",\"sql\":\"SELECT 1\",\"dialect\":\"\",\"expect\":{\"statement_count\":1}}",
		    require_id,
		    allow_dialect,
		    allow_official_item,
		    -1) != 0 ||
	    sqlparser_test_case_schema_json_result(
		    "{\"id\":\"T\",\"name\":\"n\",\"sql\":\"SELECT 1\",\"official_item\":\"\",\"expect\":{\"statement_count\":1}}",
		    require_id,
		    allow_dialect,
		    allow_official_item,
		    -1) != 0) {
		return 1;
	}
	return sqlparser_test_verify_bind_mixed_graph_field_gate();
}

static int sqlparser_test_verify_bind_mixed_rows(
	const char *case_id,
	const char *case_name,
	json_t *statements,
	json_t *expect_root)
{
	json_t *block;
	json_t *block_relations;
	json_t *blocks;
	json_t *columns;
	json_t *dml;
	json_t *expected_fragments;
	json_t *expected_relation;
	json_t *expected_columns;
	json_t *graph;
	json_t *relation;
	json_t *relations;
	json_t *rows;
	json_t *statement;
	const char *insert_mode;
	const char *kind;
	const char *relation_kind;
	size_t column_count;
	size_t expected_relation_field_count;
	size_t index;
	size_t relation_index;

	if (!sqlparser_test_is_bind_mixed_case(case_id)) {
		return 0;
	}
	if (!json_is_array(statements) ||
	    json_array_size(statements) != 1U ||
	    !json_is_object(expect_root)) {
		return sqlparser_test_fail_case(
			case_id,
			case_name,
			"bind-mixed case must contain one statement and expectations");
	}
	statement = json_array_get(statements, 0U);
	graph = json_is_object(statement) ?
		json_object_get(statement, "query_graph") :
		NULL;
	blocks = json_is_object(graph) ?
		json_object_get(graph, "blocks") :
		NULL;
	block = json_is_array(blocks) &&
		json_array_size(blocks) == 1U ?
		json_array_get(blocks, 0U) :
		NULL;
	block_relations = json_is_object(block) ?
		json_object_get(block, "relations") :
		NULL;
	dml = json_is_object(graph) ?
		json_object_get(graph, "dml") :
		NULL;
	if (!json_is_object(dml) ||
	    json_object_size(statement) != 3U ||
	    !json_is_integer(json_object_get(statement, "index")) ||
	    json_integer_value(json_object_get(statement, "index")) != 0 ||
	    !json_is_string(json_object_get(statement, "keyword")) ||
	    strcmp(
		    json_string_value(json_object_get(statement, "keyword")),
		    "insert") != 0 ||
	    json_object_size(graph) != 4U ||
	    !json_is_integer(json_object_get(graph, "root")) ||
	    json_integer_value(json_object_get(graph, "root")) != 0 ||
	    !json_is_object(block) ||
	    json_object_size(block) != 2U ||
	    !json_is_string(json_object_get(block, "kind")) ||
	    strcmp(
		    json_string_value(json_object_get(block, "kind")),
		    "select") != 0 ||
	    !json_is_array(block_relations) ||
	    json_array_size(block_relations) != 1U ||
	    !json_is_integer(json_array_get(block_relations, 0U)) ||
	    json_integer_value(json_array_get(block_relations, 0U)) != 0) {
		return sqlparser_test_fail_case(
			case_id,
			case_name,
			"bind-mixed statement or query-graph envelope is invalid");
	}
	if (!sqlparser_test_bind_mixed_graph_fields_absent(graph)) {
		return sqlparser_test_fail_case(
			case_id,
			case_name,
			"bind-mixed graph must not expose fields");
	}
	rows = json_object_get(dml, "rows");
	columns = json_object_get(dml, "target_columns");
	expected_fragments = json_object_get(expect_root, "view_contains");
	expected_relation = json_object_get(
		expect_root,
		"view_target_relation");
	expected_columns = json_object_get(
		expect_root,
		"view_target_columns");
	kind = sqlparser_test_string_value(json_object_get(dml, "kind"));
	insert_mode = sqlparser_test_string_value(
		json_object_get(dml, "insert_mode"));
	if (kind == NULL || strcmp(kind, "insert") != 0 ||
	    insert_mode == NULL || strcmp(insert_mode, "values") != 0 ||
	    json_object_size(dml) != 5U ||
	    !json_is_integer(json_object_get(dml, "target_relation")) ||
	    !json_is_array(rows) ||
	    !json_is_array(columns) ||
	    !json_is_object(expected_relation) ||
	    !json_is_array(expected_columns) ||
	    !json_is_array(expected_fragments) ||
	    json_array_size(rows) != json_array_size(expected_fragments) ||
	    json_array_size(columns) != json_array_size(expected_columns)) {
		return sqlparser_test_fail_case(
			case_id,
			case_name,
			"bind-mixed DML shape or exact cell count is invalid");
	}
	relation_index = (size_t)json_integer_value(
		json_object_get(dml, "target_relation"));
	relations = json_object_get(graph, "relations");
	relation = json_is_array(relations) &&
		relation_index < json_array_size(relations) ?
		json_array_get(relations, relation_index) :
		NULL;
	relation_kind = json_is_object(relation) ?
		sqlparser_test_string_value(
			json_object_get(relation, "kind")) :
		NULL;
	if (relation_index != 0U ||
	    !json_is_array(relations) ||
	    json_array_size(relations) != 1U ||
	    relation_kind == NULL ||
	    strcmp(relation_kind, "base") != 0 ||
	    !json_is_integer(json_object_get(relation, "block")) ||
	    json_integer_value(json_object_get(relation, "block")) != 0 ||
	    !json_is_string(json_object_get(relation, "selector")) ||
	    strcmp(
		    json_string_value(json_object_get(relation, "selector")),
		    "stmt[0].relation[0]") != 0) {
		return sqlparser_test_fail_case(
			case_id,
			case_name,
			"bind-mixed target relation is invalid");
	}
	expected_relation_field_count = 3U;
	{
		static const char *const relation_keys[] = {
			"database",
			"schema",
			"table"
		};

		for (index = 0U;
		     index < sizeof(relation_keys) / sizeof(relation_keys[0]);
		     index++) {
			const char *actual_name;
			const char *expected_name;

			actual_name = sqlparser_test_string_value(
				json_object_get(relation, relation_keys[index]));
			expected_name = sqlparser_test_string_value(
				json_object_get(
					expected_relation,
					relation_keys[index]));
			if ((actual_name == NULL) != (expected_name == NULL) ||
			    (actual_name != NULL &&
			     strcmp(actual_name, expected_name) != 0)) {
				return sqlparser_test_fail_case(
					case_id,
					case_name,
					"bind-mixed target relation differs from its exact expectation");
			}
			if (expected_name != NULL) {
				expected_relation_field_count++;
			}
		}
	}
	if (!json_is_string(json_object_get(expected_relation, "table")) ||
	    json_string_length(json_object_get(expected_relation, "table")) == 0U ||
	    json_object_size(expected_relation) !=
		    expected_relation_field_count - 3U ||
	    json_object_size(relation) != expected_relation_field_count) {
		return sqlparser_test_fail_case(
			case_id,
			case_name,
			"bind-mixed target relation shape differs from its exact expectation");
	}
	column_count = json_array_size(columns);
	if (column_count == 0U ||
	    json_array_size(rows) == 0U ||
	    json_array_size(rows) % column_count != 0U) {
		return sqlparser_test_fail_case(
			case_id,
			case_name,
			"bind-mixed row and target-column counts do not align");
	}
	for (index = 0U; index < column_count; index++) {
		json_t *column;

		column = json_array_get(columns, index);
		if (!json_is_object(column) ||
		    json_object_size(column) != 2U ||
		    !json_is_integer(json_object_get(column, "ordinal")) ||
		    json_integer_value(json_object_get(column, "ordinal")) !=
			    (json_int_t)index ||
		    !json_is_string(json_object_get(column, "column")) ||
		    json_string_length(json_object_get(column, "column")) == 0U ||
		    !json_is_string(
			    json_array_get(expected_columns, index)) ||
		    strcmp(
			    json_string_value(
				    json_object_get(column, "column")),
			    json_string_value(
				    json_array_get(expected_columns, index))) != 0) {
			return sqlparser_test_fail_case(
				case_id,
				case_name,
				"bind-mixed target columns are not exact and ordered");
		}
	}
	for (index = 0U; index < json_array_size(rows); index++) {
		const char *fragment;
		json_t *expected_cell;

		if (json_integer_value(
			    json_object_get(
				    json_array_get(rows, index),
				    "row")) !=
			    (json_int_t)(index / column_count) ||
		    json_integer_value(
			    json_object_get(
				    json_array_get(rows, index),
				    "column")) !=
			    (json_int_t)(index % column_count)) {
			return sqlparser_test_fail_case(
				case_id,
				case_name,
				"bind-mixed cells are missing, duplicated, or out of order");
		}
		fragment = sqlparser_test_string_value(
			json_array_get(expected_fragments, index));
		if (fragment == NULL) {
			return sqlparser_test_fail_case(
				case_id,
				case_name,
				"bind-mixed exact cell expectation is invalid");
		}
		expected_cell =
			sqlparser_test_bind_mixed_expected_cell(fragment);
		if (!json_is_object(expected_cell) ||
		    !json_equal(expected_cell, json_array_get(rows, index))) {
			if (expected_cell != NULL) {
				json_decref(expected_cell);
			}
			return sqlparser_test_fail_case(
				case_id,
				case_name,
				"bind-mixed DML cell differs from its exact expectation");
		}
		json_decref(expected_cell);
	}
	return 0;
}

static int sqlparser_test_json_copy_string(
	json_t *target,
	const char *key,
	json_t *source)
{
	json_t *value;

	value = json_object_get(source, key);
	if (value == NULL) {
		return 0;
	}
	if (!json_is_string(value) ||
	    json_object_set_new(
		    target,
		    key,
		    json_string(json_string_value(value))) != 0) {
		return -1;
	}
	return 0;
}

static int sqlparser_test_json_copy_integer(
	json_t *target,
	const char *key,
	json_t *source)
{
	json_t *value;

	value = json_object_get(source, key);
	if (!json_is_integer(value) ||
	    json_object_set_new(
		    target,
		    key,
		    json_integer(json_integer_value(value))) != 0) {
		return -1;
	}
	return 0;
}

static json_t *sqlparser_test_merge_relation(
	json_t *graph,
	size_t relation_index)
{
	json_t *actual;
	json_t *relations;
	json_t *result;

	relations = json_object_get(graph, "relations");
	actual = json_is_array(relations) &&
		relation_index < json_array_size(relations) ?
		json_array_get(relations, relation_index) :
		NULL;
	if (!json_is_object(actual)) {
		return NULL;
	}
	result = json_object();
	if (result == NULL ||
	    sqlparser_test_json_copy_string(
		    result, "database", actual) != 0 ||
	    sqlparser_test_json_copy_string(
		    result, "schema", actual) != 0 ||
	    sqlparser_test_json_copy_string(
		    result, "table", actual) != 0 ||
	    sqlparser_test_json_copy_string(
		    result, "alias", actual) != 0) {
		json_decref(result);
		return NULL;
	}
	if (json_object_get(actual, "source_block") != NULL &&
	    json_object_set_new(
		    result, "source_block", json_true()) != 0) {
		json_decref(result);
		return NULL;
	}
	return result;
}

static json_t *sqlparser_test_merge_field(
	json_t *graph,
	size_t field_index)
{
	json_t *actual;
	json_t *fields;
	json_t *relation;
	json_t *result;
	json_t *value;
	size_t relation_index;

	fields = json_object_get(graph, "fields");
	actual = json_is_array(fields) &&
		field_index < json_array_size(fields) ?
		json_array_get(fields, field_index) :
		NULL;
	value = json_is_object(actual) ?
		json_object_get(actual, "relation") :
		NULL;
	if (!json_is_integer(value) ||
	    json_integer_value(value) < 0 ||
	    !json_is_string(json_object_get(actual, "column"))) {
		return NULL;
	}
	relation_index = (size_t)json_integer_value(value);
	relation = sqlparser_test_merge_relation(graph, relation_index);
	result = json_object();
	if (relation == NULL ||
	    result == NULL ||
	    json_object_set(result, "relation", relation) != 0 ||
	    json_object_set_new(
		    result,
		    "column",
		    json_string(json_string_value(
			    json_object_get(actual, "column")))) != 0) {
		json_decref(relation);
		json_decref(result);
		return NULL;
	}
	json_decref(relation);
	return result;
}

static int sqlparser_test_merge_append_bind(
	json_t *result,
	json_t *actual)
{
	if (sqlparser_test_json_copy_string(
		    result, "bind_key", actual) != 0 ||
	    sqlparser_test_json_copy_integer(
		    result, "bind_kind", actual) != 0 ||
	    sqlparser_test_json_copy_string(
		    result, "bind_sql", actual) != 0 ||
	    sqlparser_test_json_copy_integer(
		    result, "bind_position", actual) != 0) {
		return -1;
	}
	return 0;
}

static json_t *sqlparser_test_merge_source_target(
	json_t *graph,
	size_t target_index)
{
	json_t *actual;
	json_t *field;
	json_t *result;
	json_t *targets;
	json_t *value;
	json_t *values;
	const char *kind;
	size_t index;

	targets = json_object_get(graph, "targets");
	actual = json_is_array(targets) &&
		target_index < json_array_size(targets) ?
		json_array_get(targets, target_index) :
		NULL;
	if (!json_is_object(actual)) {
		return NULL;
	}
	result = json_object();
	if (result == NULL ||
	    sqlparser_test_json_copy_string(result, "name", actual) != 0) {
		json_decref(result);
		return NULL;
	}
	value = json_object_get(actual, "value");
	if (json_is_integer(value) &&
	    json_integer_value(value) >= 0) {
		index = (size_t)json_integer_value(value);
		values = json_object_get(graph, "values");
		value = json_is_array(values) &&
			index < json_array_size(values) ?
			json_array_get(values, index) :
			NULL;
		kind = json_is_object(value) ?
			sqlparser_test_string_value(
				json_object_get(value, "kind")) :
			NULL;
		if (kind == NULL ||
		    json_object_set_new(
			    result, "kind", json_string(kind)) != 0) {
			json_decref(result);
			return NULL;
		}
		if (strcmp(kind, "bind") == 0 &&
		    sqlparser_test_merge_append_bind(result, value) != 0) {
			json_decref(result);
			return NULL;
		}
		if (strcmp(kind, "field") == 0) {
			json_t *source_index;

			source_index = json_object_get(value, "source_field");
			if (source_index == NULL) {
				source_index = json_object_get(value, "field");
			}
			if (!json_is_integer(source_index) ||
			    json_integer_value(source_index) < 0) {
				json_decref(result);
				return NULL;
			}
			field = sqlparser_test_merge_field(
				graph,
				(size_t)json_integer_value(source_index));
			if (field == NULL ||
			    json_object_set(
				    result, "source_field", field) != 0) {
				json_decref(field);
				json_decref(result);
				return NULL;
			}
			json_decref(field);
		}
		return result;
	}
	value = json_object_get(actual, "field");
	if (!json_is_integer(value) ||
	    json_integer_value(value) < 0 ||
	    json_object_set_new(
		    result, "kind", json_string("field")) != 0) {
		json_decref(result);
		return NULL;
	}
	field = sqlparser_test_merge_field(
		graph,
		(size_t)json_integer_value(value));
	if (field == NULL ||
	    json_object_set(
		    result, "source_field", field) != 0) {
		json_decref(field);
		json_decref(result);
		return NULL;
	}
	json_decref(field);
	return result;
}

static int sqlparser_test_merge_append_sources(
	json_t *result,
	json_t *graph,
	json_t *actual)
{
	json_t *source;
	json_t *value;

	value = json_object_get(actual, "source_field");
	if (value != NULL) {
		if (!json_is_integer(value) ||
		    json_integer_value(value) < 0) {
			return -1;
		}
		source = sqlparser_test_merge_field(
			graph,
			(size_t)json_integer_value(value));
		if (source == NULL ||
		    json_object_set(
			    result, "source_field", source) != 0) {
			json_decref(source);
			return -1;
		}
		json_decref(source);
	}
	value = json_object_get(actual, "source_target");
	if (value != NULL) {
		if (!json_is_integer(value) ||
		    json_integer_value(value) < 0) {
			return -1;
		}
		source = sqlparser_test_merge_source_target(
			graph,
			(size_t)json_integer_value(value));
		if (source == NULL ||
		    json_object_set(
			    result, "source_target", source) != 0) {
			json_decref(source);
			return -1;
		}
		json_decref(source);
	}
	return 0;
}

static json_t *sqlparser_test_merge_cell(
	json_t *graph,
	json_t *actual)
{
	json_t *result;
	json_t *value;
	const char *kind;
	size_t expected_size;

	if (!json_is_object(actual) ||
	    !json_is_integer(json_object_get(actual, "row")) ||
	    !json_is_integer(json_object_get(actual, "column")) ||
	    !json_is_integer(json_object_get(actual, "bind_kind"))) {
		return NULL;
	}
	kind = sqlparser_test_string_value(
		json_object_get(actual, "kind"));
	if (kind == NULL) {
		return NULL;
	}
	expected_size = 4U;
	if (json_object_get(actual, "source_field") != NULL) {
		expected_size++;
	}
	if (json_object_get(actual, "source_target") != NULL) {
		expected_size++;
	}
	if (strcmp(kind, "bind") == 0) {
		expected_size += 3U;
		if (!json_is_string(
			    json_object_get(actual, "bind_key")) ||
		    !json_is_string(
			    json_object_get(actual, "bind_sql")) ||
		    !json_is_integer(
			    json_object_get(actual, "bind_position"))) {
			return NULL;
		}
	} else if (json_integer_value(
			   json_object_get(actual, "bind_kind")) != 0) {
		return NULL;
	}
	if (json_object_get(actual, "selector") != NULL) {
		expected_size++;
	}
	if (json_object_get(actual, "expression_sql") != NULL) {
		expected_size++;
	}
	if (json_object_get(actual, "literal") != NULL) {
		expected_size++;
	}
	if (json_object_size(actual) != expected_size ||
	    (strcmp(kind, "field") == 0 &&
	     json_object_get(actual, "source_field") == NULL) ||
	    (strcmp(kind, "expression") == 0 &&
	     !json_is_string(
		     json_object_get(actual, "expression_sql"))) ||
	    (strcmp(kind, "expression") != 0 &&
	     json_object_get(actual, "expression_sql") != NULL)) {
		return NULL;
	}
	result = json_object();
	if (result == NULL ||
	    sqlparser_test_json_copy_integer(
		    result, "row", actual) != 0 ||
	    sqlparser_test_json_copy_integer(
		    result, "column", actual) != 0 ||
	    sqlparser_test_json_copy_string(
		    result, "kind", actual) != 0 ||
	    sqlparser_test_merge_append_sources(
		    result, graph, actual) != 0) {
		json_decref(result);
		return NULL;
	}
	if (strcmp(kind, "bind") == 0 &&
	    sqlparser_test_merge_append_bind(result, actual) != 0) {
		json_decref(result);
		return NULL;
	}
	if (sqlparser_test_json_copy_string(
		    result, "selector", actual) != 0 ||
	    sqlparser_test_json_copy_string(
		    result, "expression_sql", actual) != 0) {
		json_decref(result);
		return NULL;
	}
	value = json_object_get(actual, "literal");
	if (value != NULL &&
	    json_object_set(result, "literal", value) != 0) {
		json_decref(result);
		return NULL;
	}
	return result;
}

static json_t *sqlparser_test_merge_assignment(
	json_t *graph,
	json_t *actual,
	size_t target_relation_index)
{
	json_t *field;
	json_t *fields;
	json_t *result;
	json_t *value;
	const char *kind;
	size_t expected_size;
	size_t field_index;

	if (!json_is_object(actual) ||
	    !json_is_integer(
		    json_object_get(actual, "target_field")) ||
	    json_integer_value(
		    json_object_get(actual, "target_field")) < 0 ||
	    !json_is_integer(json_object_get(actual, "bind_kind"))) {
		return NULL;
	}
	kind = sqlparser_test_string_value(
		json_object_get(actual, "kind"));
	if (kind == NULL) {
		return NULL;
	}
	field_index = (size_t)json_integer_value(
		json_object_get(actual, "target_field"));
	fields = json_object_get(graph, "fields");
	field = json_is_array(fields) &&
		field_index < json_array_size(fields) ?
		json_array_get(fields, field_index) :
		NULL;
	value = json_is_object(field) ?
		json_object_get(field, "relation") :
		NULL;
	if (!json_is_integer(value) ||
	    json_integer_value(value) < 0 ||
	    (size_t)json_integer_value(value) !=
		    target_relation_index ||
	    !json_is_string(json_object_get(field, "column"))) {
		return NULL;
	}
	expected_size = 3U;
	if (json_object_get(actual, "source_field") != NULL) {
		expected_size++;
	}
	if (json_object_get(actual, "source_target") != NULL) {
		expected_size++;
	}
	if (strcmp(kind, "bind") == 0) {
		expected_size += 3U;
		if (!json_is_string(
			    json_object_get(actual, "bind_key")) ||
		    !json_is_string(
			    json_object_get(actual, "bind_sql")) ||
		    !json_is_integer(
			    json_object_get(actual, "bind_position"))) {
			return NULL;
		}
	} else if (json_integer_value(
			   json_object_get(actual, "bind_kind")) != 0) {
		return NULL;
	}
	if (json_object_get(actual, "selector") != NULL) {
		expected_size++;
	}
	if (json_object_get(actual, "literal") != NULL) {
		expected_size++;
	}
	if (json_object_size(actual) != expected_size ||
	    (strcmp(kind, "field") == 0 &&
	     json_object_get(actual, "source_field") == NULL)) {
		return NULL;
	}
	result = json_object();
	if (result == NULL ||
	    json_object_set_new(
		    result,
		    "target_column",
		    json_string(json_string_value(
			    json_object_get(field, "column")))) != 0 ||
	    sqlparser_test_json_copy_string(
		    result, "kind", actual) != 0 ||
	    sqlparser_test_merge_append_sources(
		    result, graph, actual) != 0) {
		json_decref(result);
		return NULL;
	}
	if (strcmp(kind, "bind") == 0 &&
	    sqlparser_test_merge_append_bind(result, actual) != 0) {
		json_decref(result);
		return NULL;
	}
	if (sqlparser_test_json_copy_string(
		    result, "selector", actual) != 0) {
		json_decref(result);
		return NULL;
	}
	value = json_object_get(actual, "literal");
	if (value != NULL &&
	    json_object_set(result, "literal", value) != 0) {
		json_decref(result);
		return NULL;
	}
	return result;
}

static json_t *sqlparser_test_merge_columns(json_t *actual)
{
	json_t *column;
	json_t *result;
	size_t index;

	if (actual == NULL) {
		return json_array();
	}
	if (!json_is_array(actual) ||
	    json_array_size(actual) == 0U) {
		return NULL;
	}
	result = json_array();
	if (result == NULL) {
		return NULL;
	}
	json_array_foreach(actual, index, column)
	{
		if (!json_is_object(column) ||
		    json_object_size(column) != 2U ||
		    !json_is_integer(
			    json_object_get(column, "ordinal")) ||
		    json_integer_value(
			    json_object_get(column, "ordinal")) !=
			    (json_int_t)index ||
		    !json_is_string(
			    json_object_get(column, "column")) ||
		    json_array_append_new(
			    result,
			    json_string(json_string_value(
				    json_object_get(
					    column, "column")))) != 0) {
			json_decref(result);
			return NULL;
		}
	}
	return result;
}

static json_t *sqlparser_test_merge_cells(
	json_t *graph,
	json_t *actual)
{
	json_t *cell;
	json_t *result;
	size_t index;

	if (actual == NULL) {
		return json_array();
	}
	if (!json_is_array(actual) ||
	    json_array_size(actual) == 0U) {
		return NULL;
	}
	result = json_array();
	if (result == NULL) {
		return NULL;
	}
	json_array_foreach(actual, index, cell)
	{
		json_t *normalized;

		normalized = sqlparser_test_merge_cell(graph, cell);
		if (normalized == NULL ||
		    json_array_append(
			    result, normalized) != 0) {
			json_decref(normalized);
			json_decref(result);
			return NULL;
		}
		json_decref(normalized);
	}
	return result;
}

static json_t *sqlparser_test_merge_assignments(
	json_t *graph,
	json_t *actual,
	size_t target_relation_index);

static json_t *sqlparser_test_merge_branch(
	const sqlparser_handle_t *handle,
	json_t *graph,
	json_t *actual,
	size_t statement_index,
	size_t target_relation_index)
{
	sqlparser_error_t error;
	sqlparser_selector_t selector;
	json_t *assignments;
	char *condition_sql;
	json_t *columns;
	json_t *item;
	json_t *result;
	json_t *rows;
	json_t *value;
	const char *action;
	const char *match;
	const char *selector_text;
	size_t index;
	size_t ordinal;
	size_t expected_size;

	if (!json_is_object(actual) ||
	    !json_is_integer(json_object_get(actual, "ordinal")) ||
	    json_integer_value(json_object_get(actual, "ordinal")) < 0 ||
	    !json_is_string(json_object_get(actual, "branch_kind")) ||
	    strcmp(
		    json_string_value(
			    json_object_get(actual, "branch_kind")),
		    "when") != 0 ||
	    !json_is_string(
		    json_object_get(actual, "merge_action_kind")) ||
	    !json_is_string(
		    json_object_get(actual, "merge_match_kind")) ||
	    !json_is_integer(
		    json_object_get(actual, "target_relation")) ||
	    json_integer_value(
		    json_object_get(actual, "target_relation")) < 0 ||
	    (size_t)json_integer_value(
		    json_object_get(actual, "target_relation")) !=
		    target_relation_index ||
	    json_object_get(actual, "condition_block") != NULL) {
		return NULL;
	}
	action = json_string_value(
		json_object_get(actual, "merge_action_kind"));
	match = json_string_value(
		json_object_get(actual, "merge_match_kind"));
	if ((strcmp(action, "insert") != 0 &&
	     strcmp(action, "update") != 0 &&
	     strcmp(action, "delete") != 0 &&
	     strcmp(action, "nothing") != 0) ||
	    (strcmp(match, "matched") != 0 &&
	     strcmp(match, "not_matched_by_source") != 0 &&
	     strcmp(match, "not_matched_by_target") != 0)) {
		return NULL;
	}
	expected_size = 5U;
	if (json_object_get(actual, "target_columns") != NULL) {
		expected_size++;
	}
	if (json_object_get(actual, "rows") != NULL) {
		expected_size++;
	}
	if (json_object_get(actual, "assignments") != NULL) {
		expected_size++;
	}
	if (json_object_get(actual, "condition_selector") != NULL) {
		expected_size++;
	}
	if (json_object_size(actual) != expected_size) {
		return NULL;
	}
	ordinal = (size_t)json_integer_value(
		json_object_get(actual, "ordinal"));
	columns = sqlparser_test_merge_columns(
		json_object_get(actual, "target_columns"));
	rows = sqlparser_test_merge_cells(
		graph,
		json_object_get(actual, "rows"));
	assignments = sqlparser_test_merge_assignments(
		graph,
		json_object_get(actual, "assignments"),
		target_relation_index);
	result = json_object();
	if (columns == NULL ||
	    rows == NULL ||
	    assignments == NULL ||
	    result == NULL ||
	    (json_array_size(columns) != 0U &&
	     json_array_size(columns) != json_array_size(rows)) ||
	    (strcmp(action, "insert") == 0 &&
	     (json_array_size(rows) == 0U ||
	      json_array_size(assignments) != 0U)) ||
	    (strcmp(action, "update") == 0 &&
	     (json_array_size(columns) != 0U ||
	      json_array_size(rows) != 0U ||
	      json_array_size(assignments) == 0U)) ||
	    ((strcmp(action, "delete") == 0 ||
	      strcmp(action, "nothing") == 0) &&
	     (json_array_size(columns) != 0U ||
	      json_array_size(rows) != 0U ||
	      json_array_size(assignments) != 0U)) ||
	    sqlparser_test_json_copy_integer(
		    result, "ordinal", actual) != 0 ||
	    sqlparser_test_json_copy_string(
		    result, "branch_kind", actual) != 0 ||
	    sqlparser_test_json_copy_string(
		    result, "merge_action_kind", actual) != 0 ||
	    sqlparser_test_json_copy_string(
		    result, "merge_match_kind", actual) != 0 ||
	    json_object_set(result, "target_columns", columns) != 0 ||
	    json_object_set(result, "rows", rows) != 0 ||
	    json_object_set(
		    result, "assignments", assignments) != 0) {
		json_decref(columns);
		json_decref(rows);
		json_decref(assignments);
		json_decref(result);
		return NULL;
	}
	json_array_foreach(rows, index, item)
	{
		if (json_integer_value(
			    json_object_get(item, "row")) !=
			    (json_int_t)ordinal ||
		    json_integer_value(
			    json_object_get(item, "column")) !=
			    (json_int_t)index) {
			json_decref(columns);
			json_decref(rows);
			json_decref(assignments);
			json_decref(result);
			return NULL;
		}
	}
	json_decref(columns);
	json_decref(rows);
	json_decref(assignments);
	value = json_object_get(actual, "condition_selector");
	if (value == NULL) {
		return result;
	}
	selector_text = sqlparser_test_string_value(value);
	if (selector_text == NULL ||
	    json_object_set_new(
		    result,
		    "condition_selector",
		    json_string(selector_text)) != 0) {
		json_decref(result);
		return NULL;
	}
	memset(&error, 0, sizeof(error));
	memset(&selector, 0, sizeof(selector));
	condition_sql = NULL;
	if (sqlparser_selector_parse(
		    selector_text,
		    &selector,
		    &error) != SQLPARSER_STATUS_OK ||
	    selector.kind !=
		    SQLPARSER_SELECTOR_KIND_MERGE_BRANCH_CONDITION ||
	    selector.statement_index != statement_index ||
	    selector.row_index != 0U ||
	    selector.item_index != ordinal ||
	    sqlparser_selector_clause_sql(
		    handle,
		    &selector,
		    &condition_sql,
		    &error) != SQLPARSER_STATUS_OK ||
	    condition_sql == NULL ||
	    condition_sql[0] == '\0' ||
	    json_object_set_new(
		    result,
		    "condition_sql",
		    json_string(condition_sql)) != 0) {
		sqlparser_string_free(condition_sql);
		json_decref(result);
		return NULL;
	}
	sqlparser_string_free(condition_sql);
	return result;
}

static json_t *sqlparser_test_merge_branches(
	const sqlparser_handle_t *handle,
	json_t *graph,
	json_t *actual,
	size_t statement_index,
	size_t target_relation_index)
{
	json_t *branch;
	json_t *result;
	size_t index;

	if (actual == NULL) {
		return json_array();
	}
	if (!json_is_array(actual) ||
	    json_array_size(actual) == 0U) {
		return NULL;
	}
	result = json_array();
	if (result == NULL) {
		return NULL;
	}
	json_array_foreach(actual, index, branch)
	{
		json_t *normalized;

		normalized = sqlparser_test_merge_branch(
			handle,
			graph,
			branch,
			statement_index,
			target_relation_index);
		if (normalized == NULL ||
		    !json_is_integer(
			    json_object_get(normalized, "ordinal")) ||
		    json_integer_value(
			    json_object_get(
				    normalized, "ordinal")) !=
			    (json_int_t)index ||
		    json_array_append(
			    result, normalized) != 0) {
			json_decref(normalized);
			json_decref(result);
			return NULL;
		}
		json_decref(normalized);
	}
	return result;
}

static json_t *sqlparser_test_merge_assignments(
	json_t *graph,
	json_t *actual,
	size_t target_relation_index)
{
	json_t *assignment;
	json_t *result;
	size_t index;

	if (actual == NULL) {
		return json_array();
	}
	if (!json_is_array(actual) ||
	    json_array_size(actual) == 0U) {
		return NULL;
	}
	result = json_array();
	if (result == NULL) {
		return NULL;
	}
	json_array_foreach(actual, index, assignment)
	{
		json_t *normalized;

		normalized = sqlparser_test_merge_assignment(
			graph,
			assignment,
			target_relation_index);
		if (normalized == NULL ||
		    json_array_append(
			    result, normalized) != 0) {
			json_decref(normalized);
			json_decref(result);
			return NULL;
		}
		json_decref(normalized);
	}
	return result;
}

static json_t *sqlparser_test_merge_actions(
	json_t *branches)
{
	json_t *branch;
	json_t *result;
	size_t index;

	if (!json_is_array(branches) ||
	    json_array_size(branches) == 0U) {
		return NULL;
	}
	result = json_array();
	if (result == NULL) {
		return NULL;
	}
	json_array_foreach(branches, index, branch)
	{
		const char *name;

		name = json_is_object(branch) ?
			sqlparser_test_string_value(
				json_object_get(
					branch,
					"merge_action_kind")) :
			NULL;
		if (name == NULL ||
		    json_array_append_new(
			    result, json_string(name)) != 0) {
			json_decref(result);
			return NULL;
		}
	}
	return result;
}

static int sqlparser_test_merge_branch_assignments_match_parent(
	json_t *branches,
	json_t *assignments)
{
	json_t *assignment;
	json_t *branch;
	json_t *branch_assignments;
	size_t assignment_index;
	size_t branch_index;
	size_t local_index;

	if (!json_is_array(branches) ||
	    !json_is_array(assignments)) {
		return 0;
	}
	assignment_index = 0U;
	json_array_foreach(branches, branch_index, branch)
	{
		branch_assignments = json_is_object(branch) ?
			json_object_get(branch, "assignments") :
			NULL;
		if (!json_is_array(branch_assignments)) {
			return 0;
		}
		json_array_foreach(
			branch_assignments, local_index, assignment)
		{
			if (assignment_index >=
				    json_array_size(assignments) ||
			    !json_equal(
				    assignment,
				    json_array_get(
					    assignments,
					    assignment_index))) {
				return 0;
			}
			assignment_index++;
		}
	}
	return assignment_index == json_array_size(assignments);
}

static int sqlparser_test_merge_bind_exists(
	json_t *array,
	json_t *bind)
{
	json_t *item;
	size_t index;

	json_array_foreach(array, index, item)
	{
		if (json_equal(item, bind)) {
			return 1;
		}
	}
	return 0;
}

static int sqlparser_test_merge_collect_binds(
	json_t *node,
	json_t *binds)
{
	const char *key;
	json_t *value;
	size_t index;

	if (json_is_object(node)) {
		int has_bind_field;

		has_bind_field =
			json_object_get(node, "bind_key") != NULL ||
			json_object_get(node, "bind_sql") != NULL ||
			json_object_get(node, "bind_position") != NULL;
		if (has_bind_field) {
			json_t *bind;

			if (!json_is_string(
				    json_object_get(node, "bind_key")) ||
			    !json_is_integer(
				    json_object_get(node, "bind_kind")) ||
			    !json_is_string(
				    json_object_get(node, "bind_sql")) ||
			    !json_is_integer(
				    json_object_get(
					    node, "bind_position"))) {
				return -1;
			}
			bind = json_object();
			if (bind == NULL ||
			    sqlparser_test_merge_append_bind(
				    bind, node) != 0) {
				json_decref(bind);
				return -1;
			}
			if (!sqlparser_test_merge_bind_exists(
				    binds, bind) &&
			    json_array_append(binds, bind) != 0) {
				json_decref(bind);
				return -1;
			}
			json_decref(bind);
		}
		json_object_foreach(node, key, value)
		{
			if (sqlparser_test_merge_collect_binds(
				    value, binds) != 0) {
				return -1;
			}
		}
		return 0;
	}
	if (json_is_array(node)) {
		json_array_foreach(node, index, value)
		{
			if (sqlparser_test_merge_collect_binds(
				    value, binds) != 0) {
				return -1;
			}
		}
	}
	return 0;
}

static json_t *sqlparser_test_merge_binds(json_t *view_root)
{
	unsigned char *used;
	json_t *binds;
	json_t *result;
	size_t count;
	size_t output_index;

	binds = json_array();
	if (binds == NULL ||
	    sqlparser_test_merge_collect_binds(
		    view_root, binds) != 0) {
		json_decref(binds);
		return NULL;
	}
	count = json_array_size(binds);
	used = count > 0U ?
		(unsigned char *)calloc(count, sizeof(*used)) :
		NULL;
	result = json_array();
	if ((count > 0U && used == NULL) ||
	    result == NULL) {
		free(used);
		json_decref(binds);
		json_decref(result);
		return NULL;
	}
	for (output_index = 0U;
	     output_index < count;
	     output_index++) {
		json_int_t best_position;
		size_t best_index;
		size_t index;

		best_index = count;
		best_position = 0;
		for (index = 0U; index < count; index++) {
			json_t *position;
			json_int_t current;

			if (used[index] != 0U) {
				continue;
			}
			position = json_object_get(
				json_array_get(binds, index),
				"bind_position");
			if (!json_is_integer(position)) {
				free(used);
				json_decref(binds);
				json_decref(result);
				return NULL;
			}
			current = json_integer_value(position);
			if (best_index == count ||
			    current < best_position) {
				best_index = index;
				best_position = current;
			}
		}
		if (best_index == count ||
		    json_array_append(
			    result,
			    json_array_get(
				    binds, best_index)) != 0) {
			free(used);
			json_decref(binds);
			json_decref(result);
			return NULL;
		}
		used[best_index] = 1U;
	}
	free(used);
	json_decref(binds);
	return result;
}

static json_t *sqlparser_test_merge_output_target(
	json_t *graph,
	json_t *references,
	size_t target_index)
{
	json_t *actual;
	json_t *field;
	json_t *fields;
	json_t *reference;
	json_t *result;
	json_t *targets;
	json_t *value;
	size_t field_index;
	size_t index;
	size_t reference_count;
	int has_field;

	field = NULL;
	field_index = 0U;
	targets = json_object_get(graph, "targets");
	actual = json_is_array(targets) &&
		target_index < json_array_size(targets) ?
		json_array_get(targets, target_index) :
		NULL;
	if (!json_is_object(actual) ||
	    !json_is_string(json_object_get(actual, "kind"))) {
		return NULL;
	}
	result = json_object();
	has_field = 0;
	if (result == NULL ||
	    sqlparser_test_json_copy_string(
		    result, "kind", actual) != 0 ||
	    sqlparser_test_json_copy_string(
		    result, "name", actual) != 0) {
		json_decref(result);
		return NULL;
	}
	value = json_object_get(actual, "field");
	if (value != NULL) {
		if (!json_is_integer(value) ||
		    json_integer_value(value) < 0) {
			json_decref(result);
			return NULL;
		}
		field_index = (size_t)json_integer_value(value);
		has_field = 1;
		fields = json_object_get(graph, "fields");
		field = json_is_array(fields) &&
			field_index < json_array_size(fields) ?
			json_array_get(fields, field_index) :
			NULL;
		if (!json_is_object(field) ||
		    !json_is_string(
			    json_object_get(field, "column")) ||
		    json_object_set_new(
			    result,
			    "column",
			    json_string(json_string_value(
				    json_object_get(
					    field, "column")))) != 0) {
			json_decref(result);
			return NULL;
		}
	}
	reference_count = 0U;
	if (references != NULL && !json_is_array(references)) {
		json_decref(result);
		return NULL;
	}
	if (json_is_array(references)) {
		json_array_foreach(references, index, reference)
		{
			value = json_is_object(reference) ?
				json_object_get(reference, "target") :
				NULL;
			if (!json_is_integer(value) ||
			    json_integer_value(value) < 0 ||
			    (size_t)json_integer_value(value) !=
				    target_index) {
				continue;
			}
			reference_count++;
			if (reference_count > 1U ||
			    !has_field ||
			    !json_is_integer(
				    json_object_get(
					    reference, "field")) ||
			    json_integer_value(
				    json_object_get(
					    reference, "field")) < 0 ||
			    (size_t)json_integer_value(
				    json_object_get(
					    reference, "field")) !=
				    field_index ||
			    !json_is_integer(
				    json_object_get(
					    reference, "relation")) ||
			    !json_is_integer(
				    json_object_get(
					    field, "relation")) ||
			    json_integer_value(
				    json_object_get(
					    reference, "relation")) !=
				    json_integer_value(
					    json_object_get(
						    field, "relation")) ||
			    !json_is_string(
				    json_object_get(
					    reference, "kind")) ||
			    json_object_set_new(
				    result,
				    "reference_kind",
				    json_string(
					    json_string_value(
						    json_object_get(
							    reference,
							    "kind")))) !=
				    0) {
				json_decref(result);
				return NULL;
			}
		}
	}
	if ((has_field && reference_count != 1U) ||
	    (!has_field && reference_count != 0U)) {
		json_decref(result);
		return NULL;
	}
	return result;
}

static json_t *sqlparser_test_merge_output(
	json_t *graph,
	json_t *dml,
	size_t *out_channel_count)
{
	json_t *block;
	json_t *blocks;
	json_t *channel;
	json_t *channels;
	json_t *references;
	json_t *result;
	json_t *target_indices;
	json_t *value;
	size_t block_index;
	size_t channel_index;
	size_t target_index;

	if (out_channel_count != NULL) {
		*out_channel_count = 0U;
	}
	channels = json_object_get(dml, "result_channels");
	if (channels == NULL) {
		return json_array();
	}
	if (!json_is_array(channels) ||
	    json_array_size(channels) == 0U) {
		return NULL;
	}
	if (out_channel_count != NULL) {
		*out_channel_count = json_array_size(channels);
	}
	result = json_array();
	if (result == NULL) {
		return NULL;
	}
	json_array_foreach(channels, channel_index, channel)
	{
		if (!json_is_object(channel)) {
			json_decref(result);
			return NULL;
		}
		value = json_object_get(channel, "block");
		if (!json_is_integer(value) ||
		    json_integer_value(value) < 0) {
			json_decref(result);
			return NULL;
		}
		block_index = (size_t)json_integer_value(value);
		blocks = json_object_get(graph, "blocks");
		block = json_is_array(blocks) &&
			block_index < json_array_size(blocks) ?
			json_array_get(blocks, block_index) :
			NULL;
		target_indices = json_is_object(block) ?
			json_object_get(block, "targets") :
			NULL;
		if (!json_is_array(target_indices) ||
		    json_array_size(target_indices) == 0U) {
			json_decref(result);
			return NULL;
		}
		references = json_object_get(channel, "references");
		json_array_foreach(
			target_indices, target_index, value)
		{
			json_t *normalized;

			if (!json_is_integer(value) ||
			    json_integer_value(value) < 0) {
				json_decref(result);
				return NULL;
			}
			normalized =
				sqlparser_test_merge_output_target(
					graph,
					references,
					(size_t)json_integer_value(
						value));
			if (normalized == NULL ||
			    json_array_append(
				    result, normalized) != 0) {
				json_decref(normalized);
				json_decref(result);
				return NULL;
			}
			json_decref(normalized);
		}
	}
	return result;
}

static int sqlparser_test_verify_merge_exact(
	const char *case_id,
	const char *case_name,
	const sqlparser_handle_t *handle,
	json_t *view_root,
	json_t *expect_root)
{
	json_t *actions;
	json_t *assignments;
	json_t *binds;
	json_t *branches;
	json_t *dml;
	json_t *expected;
	json_t *graph;
	json_t *normalized;
	json_t *output;
	json_t *parent_rows;
	json_t *parent_target_columns;
	json_t *statement;
	json_t *statements;
	json_t *target_relation;
	json_t *value;
	char *actual_text;
	char *expected_text;
	const char *insert_mode;
	const char *kind;
	size_t expected_dml_size;
	size_t output_channel_count;
	size_t statement_index;
	size_t target_relation_index;
	int failed;

	expected = expect_root != NULL ?
		json_object_get(expect_root, "view_merge_exact") :
		NULL;
	if (expected == NULL) {
		return 0;
	}
	if (handle == NULL ||
	    !json_is_object(expected) ||
	    json_object_size(expected) != 10U ||
	    !json_is_integer(
		    json_object_get(expected, "statement_index")) ||
	    json_integer_value(
		    json_object_get(
			    expected, "statement_index")) < 0 ||
	    !json_is_object(
		    json_object_get(expected, "target_relation")) ||
	    !json_is_array(json_object_get(expected, "actions")) ||
	    !json_is_array(
		    json_object_get(
			    expected, "parent_target_columns")) ||
	    json_array_size(
		    json_object_get(
			    expected, "parent_target_columns")) != 0U ||
	    !json_is_array(
		    json_object_get(expected, "parent_rows")) ||
	    json_array_size(
		    json_object_get(expected, "parent_rows")) != 0U ||
	    !json_is_array(json_object_get(expected, "binds")) ||
	    !json_is_array(
		    json_object_get(expected, "assignments")) ||
	    !json_is_array(json_object_get(expected, "branches")) ||
	    !json_is_integer(
		    json_object_get(expected, "output_channels")) ||
	    json_integer_value(
		    json_object_get(
			    expected, "output_channels")) < 0 ||
	    !json_is_array(json_object_get(expected, "output"))) {
		return sqlparser_test_fail_case(
			case_id,
			case_name,
			"view_merge_exact expectation is invalid");
	}
	statement_index = (size_t)json_integer_value(
		json_object_get(expected, "statement_index"));
	statements = json_object_get(view_root, "statements");
	statement = json_is_array(statements) &&
		statement_index < json_array_size(statements) ?
		json_array_get(statements, statement_index) :
		NULL;
	graph = json_is_object(statement) ?
		json_object_get(statement, "query_graph") :
		NULL;
	dml = json_is_object(graph) ?
		json_object_get(graph, "dml") :
		NULL;
	kind = json_is_object(dml) ?
		sqlparser_test_string_value(
			json_object_get(dml, "kind")) :
		NULL;
	insert_mode = json_is_object(dml) ?
		sqlparser_test_string_value(
			json_object_get(dml, "insert_mode")) :
		NULL;
	value = json_is_object(dml) ?
		json_object_get(dml, "target_relation") :
		NULL;
	if (kind == NULL ||
	    strcmp(kind, "merge") != 0 ||
	    insert_mode == NULL ||
	    strcmp(insert_mode, "unknown") != 0 ||
	    !json_is_integer(value) ||
	    json_integer_value(value) < 0 ||
	    json_object_get(dml, "target_columns") != NULL ||
	    json_object_get(dml, "rows") != NULL ||
	    json_object_get(dml, "delete_targets") != NULL ||
	    json_object_get(dml, "children") != NULL) {
		return sqlparser_test_fail_case(
			case_id,
			case_name,
			"MERGE DML parent shape is not branch-scoped");
	}
	target_relation_index =
		(size_t)json_integer_value(value);
	expected_dml_size = 3U;
	if (json_object_get(dml, "assignments") != NULL) {
		expected_dml_size++;
	}
	if (json_object_get(dml, "branches") != NULL) {
		expected_dml_size++;
	}
	if (json_object_get(dml, "result_channels") != NULL) {
		expected_dml_size++;
	}
	if (json_object_size(dml) != expected_dml_size) {
		return sqlparser_test_fail_case(
			case_id,
			case_name,
			"MERGE DML contains unexpected parent fields");
	}
	target_relation = sqlparser_test_merge_relation(
		graph, target_relation_index);
	binds = sqlparser_test_merge_binds(view_root);
	assignments = sqlparser_test_merge_assignments(
		graph,
		json_object_get(dml, "assignments"),
		target_relation_index);
	branches = sqlparser_test_merge_branches(
		handle,
		graph,
		json_object_get(dml, "branches"),
		statement_index,
		target_relation_index);
	actions = sqlparser_test_merge_actions(branches);
	output = sqlparser_test_merge_output(
		graph,
		dml,
		&output_channel_count);
	parent_target_columns = json_array();
	parent_rows = json_array();
	normalized = json_object();
	if (target_relation == NULL ||
	    actions == NULL ||
	    binds == NULL ||
	    assignments == NULL ||
	    branches == NULL ||
	    !sqlparser_test_merge_branch_assignments_match_parent(
		    branches, assignments) ||
	    output == NULL ||
	    parent_target_columns == NULL ||
	    parent_rows == NULL ||
	    normalized == NULL ||
	    json_object_set_new(
		    normalized,
		    "statement_index",
		    json_integer((json_int_t)statement_index)) != 0 ||
	    json_object_set(
		    normalized,
		    "target_relation",
		    target_relation) != 0 ||
	    json_object_set(normalized, "actions", actions) != 0 ||
	    json_object_set(
		    normalized,
		    "parent_target_columns",
		    parent_target_columns) != 0 ||
	    json_object_set(
		    normalized, "parent_rows", parent_rows) != 0 ||
	    json_object_set(normalized, "binds", binds) != 0 ||
	    json_object_set(
		    normalized, "assignments", assignments) != 0 ||
	    json_object_set(
		    normalized, "branches", branches) != 0 ||
	    json_object_set_new(
		    normalized,
		    "output_channels",
		    json_integer(
			    (json_int_t)output_channel_count)) != 0 ||
	    json_object_set(normalized, "output", output) != 0) {
		json_decref(target_relation);
		json_decref(actions);
		json_decref(binds);
		json_decref(assignments);
		json_decref(branches);
		json_decref(output);
		json_decref(parent_target_columns);
		json_decref(parent_rows);
		json_decref(normalized);
		return sqlparser_test_fail_case(
			case_id,
			case_name,
			"MERGE exact normalization failed");
	}
	json_decref(target_relation);
	json_decref(actions);
	json_decref(binds);
	json_decref(assignments);
	json_decref(branches);
	json_decref(output);
	json_decref(parent_target_columns);
	json_decref(parent_rows);
	failed = !json_equal(normalized, expected);
	if (failed) {
		actual_text = json_dumps(
			normalized,
			JSON_COMPACT | JSON_SORT_KEYS);
		expected_text = json_dumps(
			expected,
			JSON_COMPACT | JSON_SORT_KEYS);
		fprintf(
			stderr,
			"FAIL [%s %s]: MERGE exact expectation mismatch\n"
			"  expected: %s\n  actual:   %s\n",
			case_id != NULL ? case_id : "-",
			case_name != NULL ? case_name : "-",
			expected_text != NULL ? expected_text : "-",
			actual_text != NULL ? actual_text : "-");
		free(actual_text);
		free(expected_text);
	}
	json_decref(normalized);
	return failed;
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
	if (sqlparser_test_verify_dml_shape(
		    case_id,
		    case_name,
		    json_object_get(graph, "dml")) != 0) {
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
	const sqlparser_handle_t *handle,
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
	if (handle == NULL ||
	    json_array_size(statements) !=
		    sqlparser_statement_count(handle)) {
		json_decref(root);
		return sqlparser_test_fail_case(
			case_id,
			case_name,
			"view JSON statement count differs from the handle");
	}
	json_array_foreach(statements, index, statement)
	{
		json_t *statement_index;

		statement_index = json_is_object(statement) ?
			json_object_get(statement, "index") :
			NULL;
		if (!json_is_integer(statement_index) ||
		    json_integer_value(statement_index) !=
			    (json_int_t)index) {
			json_decref(root);
			return sqlparser_test_fail_case(
				case_id,
				case_name,
				"view JSON statement index is not exact and ordered");
		}
		if (sqlparser_test_verify_query_graph_shape(case_id, case_name, statement) != 0) {
			json_decref(root);
			return 1;
		}
	}
	if (sqlparser_test_verify_bind_mixed_rows(
		    case_id,
		    case_name,
		    statements,
		    expect_root) != 0) {
		json_decref(root);
		return 1;
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
	const sqlparser_handle_t *handle,
	const char *view_json,
	json_t *expect_root)
{
	json_error_t error;
	json_t *root;

	if (expect_root == NULL) {
		return sqlparser_test_verify_view_shape(
			case_id,
			case_name,
			handle,
			view_json,
			NULL);
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
	if (sqlparser_test_verify_view_shape(
		    case_id,
		    case_name,
		    handle,
		    view_json,
		    expect_root) != 0) {
		return 1;
	}
	root = json_loads(view_json, 0, &error);
	if (root == NULL) {
		return sqlparser_test_fail_case(
			case_id,
			case_name,
			"view JSON should decode for exact MERGE validation");
	}
	if (sqlparser_test_verify_merge_exact(
		    case_id,
		    case_name,
		    handle,
		    root,
		    expect_root) != 0) {
		json_decref(root);
		return 1;
	}
	json_decref(root);
	return 0;
}

#endif
