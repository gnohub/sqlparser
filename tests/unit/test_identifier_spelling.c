#include <stdio.h>
#include <string.h>

#include "pg_query.h"
#include "sqlparser/sqlparser.h"
#include "sqlparser_internal.h"
#include "../../src/core/sqlparser_ast_internal.h"

typedef struct {
	const char *owner_type;
	const char *field_name;
	const char *value;
} expected_name_t;

typedef struct {
	sqlparser_dialect_t dialect;
	const char *sql;
	size_t statement_index;
	expected_name_t names[8];
	size_t name_count;
} spelling_case_t;

static int statement_has_name(
	sqlparser_handle_t *handle,
	size_t statement_index,
	const expected_name_t *expected)
{
	sqlparser_error_t error;
	sqlparser_name_view_t name;
	size_t count;
	size_t index;

	memset(&error, 0, sizeof(error));
	if (sqlparser_statement_name_count(
		    handle,
		    statement_index,
		    &count,
		    &error) != SQLPARSER_STATUS_OK) {
		fprintf(stderr, "FAIL: AST name count failed: %s\n", error.message);
		return 0;
	}
	for (index = 0U; index < count; index++) {
		memset(&name, 0, sizeof(name));
		if (sqlparser_statement_name(
			    handle,
			    statement_index,
			    index,
			    &name,
			    &error) != SQLPARSER_STATUS_OK) {
			fprintf(stderr, "FAIL: AST name lookup failed: %s\n", error.message);
			return 0;
		}
		if (name.owner_type != NULL &&
		    name.field_name != NULL &&
		    name.value != NULL &&
		    strcmp(name.owner_type, expected->owner_type) == 0 &&
		    strcmp(name.field_name, expected->field_name) == 0 &&
		    strcmp(name.value, expected->value) == 0) {
			return 1;
		}
	}
	return 0;
}

static int statement_find_name_index(
	sqlparser_handle_t *handle,
	size_t statement_index,
	const char *owner_type,
	const char *field_name,
	const char *value,
	size_t *out_index)
{
	sqlparser_error_t error;
	sqlparser_name_view_t name;
	size_t count;
	size_t index;

	*out_index = (size_t)-1;
	memset(&error, 0, sizeof(error));
	if (sqlparser_statement_name_count(
		    handle,
		    statement_index,
		    &count,
		    &error) != SQLPARSER_STATUS_OK) {
		return 0;
	}
	for (index = 0U; index < count; index++) {
		memset(&name, 0, sizeof(name));
		if (sqlparser_statement_name(
			    handle,
			    statement_index,
			    index,
			    &name,
			    &error) == SQLPARSER_STATUS_OK &&
		    name.owner_type != NULL &&
		    name.field_name != NULL &&
		    name.value != NULL &&
		    strcmp(name.owner_type, owner_type) == 0 &&
		    strcmp(name.field_name, field_name) == 0 &&
		    strcmp(name.value, value) == 0) {
			*out_index = index;
			return 1;
		}
	}
	return 0;
}

static int standard_parse_has_spelling(
	const char *sql,
	const char *expected,
	const char *unexpected)
{
	PgQueryParseResult result;
	int valid;

	result = pg_query_parse(sql);
	if (result.error != NULL) {
		fprintf(
			stderr,
			"FAIL: standard libpg_query parse failed: %s\n",
			result.error->message);
		pg_query_free_parse_result(result);
		return 0;
	}
	valid =
		result.parse_tree != NULL &&
		strstr(result.parse_tree, expected) != NULL &&
		(unexpected == NULL ||
		 strstr(result.parse_tree, unexpected) == NULL);
	if (!valid) {
		fprintf(
			stderr,
			"FAIL: standard libpg_query identifier contract changed: %s\n",
			sql);
	}
	pg_query_free_parse_result(result);
	return valid;
}

static int standard_protobuf_deparse_has_spelling(
	const char *sql,
	const char *expected,
	const char *unexpected)
{
	PgQueryProtobufParseResult parse_result;
	PgQueryDeparseResult deparse_result;
	int valid;

	parse_result = pg_query_parse_protobuf(sql);
	if (parse_result.error != NULL) {
		fprintf(
			stderr,
			"FAIL: standard protobuf parse failed: %s\n",
			parse_result.error->message);
		pg_query_free_protobuf_parse_result(parse_result);
		return 0;
	}
	deparse_result = pg_query_deparse_protobuf(parse_result.parse_tree);
	valid =
		deparse_result.error == NULL &&
		deparse_result.query != NULL &&
		strstr(deparse_result.query, expected) != NULL &&
		(unexpected == NULL ||
		 strstr(deparse_result.query, unexpected) == NULL);
	if (!valid) {
		fprintf(
			stderr,
			"FAIL: standard protobuf identifier contract changed: %s\n",
			sql);
	}
	pg_query_free_deparse_result(deparse_result);
	pg_query_free_protobuf_parse_result(parse_result);
	return valid;
}

static int sqlparser_parse_is_rejected(const char *sql)
{
	sqlparser_handle_t *handle;
	sqlparser_error_t error;
	sqlparser_status_t status;

	handle = NULL;
	memset(&error, 0, sizeof(error));
	status = sqlparser_parse(sql, &handle, &error);
	sqlparser_handle_destroy(handle);
	return status == SQLPARSER_STATUS_PARSE_ERROR;
}

static int standard_fingerprints_match(const char *left_sql, const char *right_sql)
{
	PgQueryFingerprintResult left;
	PgQueryFingerprintResult right;
	int valid;

	left = pg_query_fingerprint(left_sql);
	right = pg_query_fingerprint(right_sql);
	valid =
		left.error == NULL &&
		right.error == NULL &&
		left.fingerprint_str != NULL &&
		right.fingerprint_str != NULL &&
		strcmp(left.fingerprint_str, right.fingerprint_str) == 0;
	if (!valid) {
		fprintf(
			stderr,
			"FAIL: standard libpg_query fingerprint canonicalization changed\n");
	}
	pg_query_free_fingerprint_result(left);
	pg_query_free_fingerprint_result(right);
	return valid;
}

static int xmltable_internal_option_semantics_match(void)
{
	static const struct {
		const char *option;
		const char *message;
	} cases[] = {
		{
			"__PG__IS_NOT_NULL",
			"cannot be used in XMLTABLE"
		},
		{
			"\"__pg__is_not_null\"",
			"cannot be used in XMLTABLE"
		},
		{
			"\"__PG__IS_NOT_NULL\"",
			"unrecognized column option"
		}
	};
	char sql[256];
	size_t index;

	for (index = 0U; index < sizeof(cases) / sizeof(cases[0]); index++) {
		PgQueryProtobufParseResult preserve_result;
		PgQueryProtobufParseResult standard_result;
		int valid;

		(void)snprintf(
			sql,
			sizeof(sql),
			"SELECT * FROM XMLTABLE('/a' PASSING '<a/>' "
			"COLUMNS x text %s true)",
			cases[index].option);
		standard_result = pg_query_parse_protobuf(sql);
		preserve_result =
			sqlparser_parse_protobuf_preserving_identifier_spelling(
				sql);
		valid =
			standard_result.error != NULL &&
			standard_result.error->message != NULL &&
			strstr(
				standard_result.error->message,
				cases[index].message) != NULL &&
			preserve_result.error != NULL &&
			preserve_result.error->message != NULL &&
			strstr(
				preserve_result.error->message,
				cases[index].message) != NULL;
		if (!valid) {
			fprintf(
				stderr,
				"FAIL: XMLTABLE option semantics changed: %s\n",
				sql);
		}
		pg_query_free_protobuf_parse_result(preserve_result);
		pg_query_free_protobuf_parse_result(standard_result);
		if (!valid) {
			return 0;
		}
	}
	return 1;
}

static int query_graph_identifier_semantics(void)
{
	static const struct {
		const char *sql;
		const char *relation_name;
		sqlparser_graph_relation_kind_t expected_kind;
	} relation_cases[] = {
		{
			"WITH \"C\" AS (SELECT 1 AS id) SELECT * FROM \"c\"",
			"c",
			SQLPARSER_GRAPH_REL_BASE
		},
		{
			"SELECT 1 FROM \"DUAL\"",
			"DUAL",
			SQLPARSER_GRAPH_REL_BASE
		}
	};
	static const struct {
		sqlparser_dialect_t dialect;
		const char *sql;
		int expected_has_relation;
	} alias_cases[] = {
		{
			SQLPARSER_DIALECT_POSTGRESQL,
			"SELECT \"T\".id FROM foo AS \"t\"",
			0
		},
		{
			SQLPARSER_DIALECT_POSTGRESQL,
			"SELECT T.id FROM foo AS t",
			1
		},
		{
			SQLPARSER_DIALECT_ORACLE,
			"SELECT \"T\".id FROM foo \"t\"",
			0
		},
		{
			SQLPARSER_DIALECT_ORACLE,
			"SELECT t.id FROM foo T",
			1
		}
	};
	static const char prefix_63[] =
		"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
		"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
	sqlparser_graph_field_t field;
	sqlparser_graph_relation_t relation;
	sqlparser_handle_t *handle;
	sqlparser_query_graph_view_t graph;
	sqlparser_error_t error;
	sqlparser_parse_options_t options;
	char sql[256];
	size_t case_index;
	size_t index;
	int found;

	for (case_index = 0U;
	     case_index < sizeof(relation_cases) / sizeof(relation_cases[0]);
	     case_index++) {
		handle = NULL;
		memset(&error, 0, sizeof(error));
		if (sqlparser_parse(
			    relation_cases[case_index].sql,
			    &handle,
			    &error) != SQLPARSER_STATUS_OK ||
		    sqlparser_statement_query_graph(
			    handle,
			    0U,
			    &graph,
			    &error) != SQLPARSER_STATUS_OK) {
			fprintf(
				stderr,
				"FAIL: identifier graph relation case failed: %s\n",
				error.message);
			sqlparser_handle_destroy(handle);
			return 0;
		}
		found = 0;
		for (index = 0U; index < graph.relation_count; index++) {
			if (sqlparser_query_graph_relation_at(
				    &graph,
				    index,
				    &relation,
				    &error) == SQLPARSER_STATUS_OK &&
			    relation.object_name != NULL &&
			    strcmp(
				    relation.object_name,
				    relation_cases[case_index].relation_name) == 0 &&
			    relation.kind ==
				    relation_cases[case_index].expected_kind) {
				found = 1;
				break;
			}
		}
		sqlparser_handle_destroy(handle);
		if (!found) {
			fprintf(
				stderr,
				"FAIL: identifier graph relation semantics changed: %s\n",
				relation_cases[case_index].sql);
			return 0;
		}
	}

	for (case_index = 0U;
	     case_index < sizeof(alias_cases) / sizeof(alias_cases[0]);
	     case_index++) {
		handle = NULL;
		memset(&error, 0, sizeof(error));
		sqlparser_parse_options_default(&options);
		options.dialect = alias_cases[case_index].dialect;
		if (sqlparser_parse_with_options(
			    alias_cases[case_index].sql,
			    &options,
			    &handle,
			    &error) != SQLPARSER_STATUS_OK ||
		    sqlparser_statement_query_graph(
			    handle,
			    0U,
			    &graph,
			    &error) != SQLPARSER_STATUS_OK) {
			fprintf(
				stderr,
				"FAIL: alias graph case failed: %s\n",
				error.message);
			sqlparser_handle_destroy(handle);
			return 0;
		}
		found = 0;
		for (index = 0U; index < graph.field_count; index++) {
			if (sqlparser_query_graph_field_at(
				    &graph,
				    index,
				    &field,
				    &error) == SQLPARSER_STATUS_OK &&
			    field.column_name != NULL &&
			    strcmp(field.column_name, "id") == 0 &&
			    field.has_relation ==
				    alias_cases[case_index].expected_has_relation) {
				found = 1;
				break;
			}
		}
		sqlparser_handle_destroy(handle);
		if (!found) {
			fprintf(
				stderr,
				"FAIL: identifier alias semantics changed: %s\n",
				alias_cases[case_index].sql);
			return 0;
		}
	}

	if (snprintf(
		    sql,
		    sizeof(sql),
		    "SELECT %sY.id FROM foo AS %sX",
		    prefix_63,
		    prefix_63) < 0) {
		return 0;
	}
	handle = NULL;
	memset(&error, 0, sizeof(error));
	if (sqlparser_parse(sql, &handle, &error) != SQLPARSER_STATUS_OK ||
	    sqlparser_statement_query_graph(
		    handle,
		    0U,
		    &graph,
		    &error) != SQLPARSER_STATUS_OK) {
		fprintf(stderr, "FAIL: long identifier graph case failed: %s\n", error.message);
		sqlparser_handle_destroy(handle);
		return 0;
	}
	found = 0;
	for (index = 0U; index < graph.field_count; index++) {
		if (sqlparser_query_graph_field_at(
			    &graph,
			    index,
			    &field,
			    &error) == SQLPARSER_STATUS_OK &&
		    field.column_name != NULL &&
		    strcmp(field.column_name, "id") == 0 &&
		    field.has_relation) {
			found = 1;
			break;
		}
	}
	sqlparser_handle_destroy(handle);
	if (!found) {
		fprintf(stderr, "FAIL: PostgreSQL 63-byte identifier keys did not match\n");
		return 0;
	}
	return 1;
}

static int session_identifier_semantics(void)
{
	static const struct {
		const char *sql;
		sqlparser_graph_session_value_kind_t expected_kind;
	} cases[] = {
		{
			"SET search_path TO \"NONE\"",
			SQLPARSER_GRAPH_SESSION_VALUE_IDENTIFIER
		},
		{
			"SET search_path TO NONE",
			SQLPARSER_GRAPH_SESSION_VALUE_KEYWORD
		},
		{
			"SET ROLE \"CURRENT\"",
			SQLPARSER_GRAPH_SESSION_VALUE_IDENTIFIER
		},
		{
			"SET ROLE CURRENT",
			SQLPARSER_GRAPH_SESSION_VALUE_KEYWORD
		}
	};
	sqlparser_graph_session_item_t item;
	sqlparser_graph_session_value_t value;
	sqlparser_handle_t *handle;
	sqlparser_query_graph_view_t graph;
	sqlparser_error_t error;
	size_t index;

	for (index = 0U; index < sizeof(cases) / sizeof(cases[0]); index++) {
		handle = NULL;
		memset(&error, 0, sizeof(error));
		if (sqlparser_parse(cases[index].sql, &handle, &error) !=
			    SQLPARSER_STATUS_OK ||
		    sqlparser_statement_query_graph(
			    handle,
			    0U,
			    &graph,
			    &error) != SQLPARSER_STATUS_OK ||
		    sqlparser_query_graph_session_item_at(
			    &graph,
			    0U,
			    &item,
			    &error) != SQLPARSER_STATUS_OK ||
		    item.value_count != 1U ||
		    sqlparser_query_graph_session_value_at(
			    &graph,
			    item.value_offset,
			    &value,
			    &error) != SQLPARSER_STATUS_OK ||
		    value.kind != cases[index].expected_kind) {
			fprintf(
				stderr,
				"FAIL: session identifier semantics changed for %s: %s\n",
				cases[index].sql,
				error.message);
			sqlparser_handle_destroy(handle);
			return 0;
		}
		sqlparser_handle_destroy(handle);
	}
	return 1;
}

static int ast_deparse_preserves_dialect(
	sqlparser_dialect_t dialect,
	const char *sql,
	const char *required,
	const char *also_required,
	const char *forbidden)
{
	PgQueryDeparseResult deparse_result;
	sqlparser_handle_t *handle;
	sqlparser_error_t error;
	sqlparser_parse_options_t options;
	int valid;

	handle = NULL;
	memset(&error, 0, sizeof(error));
	sqlparser_parse_options_default(&options);
	options.dialect = dialect;
	if (sqlparser_parse_with_options(
		    sql,
		    &options,
		    &handle,
		    &error) != SQLPARSER_STATUS_OK) {
		fprintf(stderr, "FAIL: AST deparse canary did not parse: %s\n", sql);
		return 0;
	}
	if (sqlparser_validate_ast_identifier_spelling(handle, &error) !=
	    SQLPARSER_STATUS_OK) {
		fprintf(
			stderr,
			"FAIL: AST deparse canary spelling audit failed for %s: %s\n",
			sql,
			error.message);
		sqlparser_handle_destroy(handle);
		return 0;
	}
	deparse_result =
		sqlparser_deparse_protobuf_for_handle(
			handle,
			handle->parse_tree,
			0U,
			0U,
			handle->parser_sql_len);
	valid =
		deparse_result.error == NULL &&
		deparse_result.query != NULL &&
		(required == NULL ||
		 strstr(deparse_result.query, required) != NULL) &&
		(also_required == NULL ||
		 strstr(deparse_result.query, also_required) != NULL) &&
		(forbidden == NULL ||
		 strstr(deparse_result.query, forbidden) == NULL);
	if (!valid) {
		fprintf(
			stderr,
			"FAIL: AST deparse canary changed source spelling: %s\noutput: %s\n",
			sql,
			deparse_result.query != NULL ?
				deparse_result.query :
				"(null)");
	}
	pg_query_free_deparse_result(deparse_result);
	sqlparser_handle_destroy(handle);
	return valid;
}

static int ast_deparse_preserves(
	const char *sql,
	const char *required,
	const char *also_required,
	const char *forbidden)
{
	return ast_deparse_preserves_dialect(
		SQLPARSER_DIALECT_POSTGRESQL,
		sql,
		required,
		also_required,
		forbidden);
}

static int mutated_keyword_alias_is_quoted(void)
{
	sqlparser_handle_t *handle;
	sqlparser_name_view_t name;
	sqlparser_error_t error;
	sqlparser_status_t status;
	char *deparsed;
	size_t count;
	size_t index;
	size_t target_index;

	handle = NULL;
	deparsed = NULL;
	target_index = (size_t)-1;
	memset(&error, 0, sizeof(error));
	if (sqlparser_parse(
		    "SELECT 1 AS select FROM t",
		    &handle,
		    &error) != SQLPARSER_STATUS_OK ||
	    sqlparser_statement_name_count(
		    handle,
		    0U,
		    &count,
		    &error) != SQLPARSER_STATUS_OK) {
		fprintf(stderr, "FAIL: keyword alias mutation setup failed\n");
		sqlparser_handle_destroy(handle);
		return 0;
	}
	for (index = 0U; index < count; index++) {
		if (sqlparser_statement_name(
			    handle,
			    0U,
			    index,
			    &name,
			    &error) == SQLPARSER_STATUS_OK &&
		    name.owner_type != NULL &&
		    name.field_name != NULL &&
		    name.value != NULL &&
		    strcmp(name.owner_type, "ResTarget") == 0 &&
		    strcmp(name.field_name, "name") == 0 &&
		    strcmp(name.value, "select") == 0) {
			target_index = index;
			break;
		}
	}
	if (target_index == (size_t)-1 ||
	    sqlparser_statement_set_name(
		    handle,
		    0U,
		    target_index,
		    "FROM",
		    &error) != SQLPARSER_STATUS_OK) {
		fprintf(stderr, "FAIL: keyword alias mutation failed\n");
		sqlparser_handle_destroy(handle);
		return 0;
	}
	status = sqlparser_deparse(handle, &deparsed, &error);
	if (status != SQLPARSER_STATUS_OK ||
	    deparsed == NULL ||
	    strstr(deparsed, "AS \"FROM\" FROM t") == NULL) {
		fprintf(
			stderr,
			"FAIL: keyword alias mutation borrowed a syntax token: %s\n",
			deparsed != NULL ? deparsed : error.message);
		sqlparser_string_free(deparsed);
		sqlparser_handle_destroy(handle);
		return 0;
	}
	sqlparser_string_free(deparsed);
	sqlparser_handle_destroy(handle);
	return 1;
}

static int mutated_type_prefix_is_not_synthetic(void)
{
	sqlparser_handle_t *handle;
	sqlparser_name_view_t name;
	sqlparser_error_t error;
	char *deparsed;
	size_t count;
	size_t index;
	size_t target_index;

	handle = NULL;
	deparsed = NULL;
	target_index = (size_t)-1;
	memset(&error, 0, sizeof(error));
	if (sqlparser_parse(
		    "SELECT 1::foo.bit",
		    &handle,
		    &error) != SQLPARSER_STATUS_OK ||
	    sqlparser_statement_name_count(
		    handle,
		    0U,
		    &count,
		    &error) != SQLPARSER_STATUS_OK) {
		fprintf(stderr, "FAIL: type prefix mutation setup failed\n");
		sqlparser_handle_destroy(handle);
		return 0;
	}
	for (index = 0U; index < count; index++) {
		if (sqlparser_statement_name(
			    handle,
			    0U,
			    index,
			    &name,
			    &error) == SQLPARSER_STATUS_OK &&
		    name.owner_type != NULL &&
		    name.field_name != NULL &&
		    name.value != NULL &&
		    strcmp(name.owner_type, "TypeName") == 0 &&
		    strcmp(name.field_name, "names") == 0 &&
		    strcmp(name.value, "foo") == 0) {
			target_index = index;
			break;
		}
	}
	if (target_index == (size_t)-1 ||
	    sqlparser_statement_set_name(
		    handle,
		    0U,
		    target_index,
		    "pg_catalog",
		    &error) != SQLPARSER_STATUS_OK ||
	    sqlparser_deparse(handle, &deparsed, &error) !=
		    SQLPARSER_STATUS_OK ||
	    deparsed == NULL ||
	    strstr(deparsed, "::pg_catalog.bit") == NULL) {
		fprintf(
			stderr,
			"FAIL: generated pg_catalog type prefix was treated as synthetic: %s\n",
			deparsed != NULL ? deparsed : error.message);
		sqlparser_string_free(deparsed);
		sqlparser_handle_destroy(handle);
		return 0;
	}
	sqlparser_string_free(deparsed);
	sqlparser_handle_destroy(handle);
	return 1;
}

static int mutated_name_preserves_siblings(
	const char *sql,
	const char *owner_type,
	const char *field_name,
	const char *old_value,
	const char *new_value,
	const char *required,
	const char *forbidden)
{
	sqlparser_handle_t *handle;
	sqlparser_handle_t *reparsed;
	sqlparser_name_view_t name;
	sqlparser_error_t error;
	char *deparsed;
	size_t count;
	size_t index;
	size_t target_index;

	handle = NULL;
	reparsed = NULL;
	deparsed = NULL;
	target_index = (size_t)-1;
	memset(&error, 0, sizeof(error));
	if (sqlparser_parse(sql, &handle, &error) != SQLPARSER_STATUS_OK ||
	    sqlparser_statement_name_count(
		    handle,
		    0U,
		    &count,
		    &error) != SQLPARSER_STATUS_OK) {
		fprintf(stderr, "FAIL: sibling spelling mutation setup failed: %s\n", sql);
		sqlparser_handle_destroy(handle);
		return 0;
	}
	for (index = 0U; index < count; index++) {
		if (sqlparser_statement_name(
			    handle,
			    0U,
			    index,
			    &name,
			    &error) == SQLPARSER_STATUS_OK &&
		    name.owner_type != NULL &&
		    name.field_name != NULL &&
		    name.value != NULL &&
		    strcmp(name.owner_type, owner_type) == 0 &&
		    strcmp(name.field_name, field_name) == 0 &&
		    strcmp(name.value, old_value) == 0) {
			target_index = index;
			break;
		}
	}
	if (target_index == (size_t)-1 ||
	    sqlparser_statement_set_name(
		    handle,
		    0U,
		    target_index,
		    new_value,
		    &error) != SQLPARSER_STATUS_OK ||
	    sqlparser_deparse(handle, &deparsed, &error) !=
		    SQLPARSER_STATUS_OK ||
	    deparsed == NULL ||
	    strstr(deparsed, required) == NULL ||
	    (forbidden != NULL && strstr(deparsed, forbidden) != NULL) ||
	    sqlparser_parse(deparsed, &reparsed, &error) !=
		    SQLPARSER_STATUS_OK) {
		fprintf(
			stderr,
			"FAIL: name mutation changed a sibling identifier: %s\noutput: %s\n",
			sql,
			deparsed != NULL ? deparsed : error.message);
		sqlparser_handle_destroy(reparsed);
		sqlparser_string_free(deparsed);
		sqlparser_handle_destroy(handle);
		return 0;
	}
	sqlparser_handle_destroy(reparsed);
	sqlparser_string_free(deparsed);
	sqlparser_handle_destroy(handle);
	return 1;
}

static int mutated_dialect_keeps_unmodified_spelling(
	sqlparser_dialect_t dialect,
	const char *sql,
	const char *old_alias,
	const char *new_alias,
	const char *expected)
{
	sqlparser_handle_t *handle;
	sqlparser_name_view_t name;
	sqlparser_parse_options_t options;
	sqlparser_error_t error;
	char *deparsed;
	size_t count;
	size_t index;
	size_t target_index;

	handle = NULL;
	deparsed = NULL;
	target_index = (size_t)-1;
	memset(&error, 0, sizeof(error));
	sqlparser_parse_options_default(&options);
	options.dialect = dialect;
	if (sqlparser_parse_with_options(
		    sql,
		    &options,
		    &handle,
		    &error) != SQLPARSER_STATUS_OK ||
	    sqlparser_statement_name_count(
		    handle,
		    0U,
		    &count,
		    &error) != SQLPARSER_STATUS_OK) {
		fprintf(stderr, "FAIL: transformed dialect mutation setup failed\n");
		sqlparser_handle_destroy(handle);
		return 0;
	}
	for (index = 0U; index < count; index++) {
		if (sqlparser_statement_name(
			    handle,
			    0U,
			    index,
			    &name,
			    &error) == SQLPARSER_STATUS_OK &&
		    name.owner_type != NULL &&
		    name.field_name != NULL &&
		    name.value != NULL &&
		    strcmp(name.owner_type, "ResTarget") == 0 &&
		    strcmp(name.field_name, "name") == 0 &&
		    strcmp(name.value, old_alias) == 0) {
			target_index = index;
			break;
		}
	}
	if (target_index == (size_t)-1 ||
	    sqlparser_statement_set_name(
		    handle,
		    0U,
		    target_index,
		    new_alias,
		    &error) != SQLPARSER_STATUS_OK ||
	    sqlparser_deparse(handle, &deparsed, &error) !=
		    SQLPARSER_STATUS_OK ||
	    deparsed == NULL ||
	    strstr(deparsed, expected) == NULL) {
		fprintf(
			stderr,
			"FAIL: transformed dialect mutation changed an unmodified identifier: %s\n",
			deparsed != NULL ? deparsed : error.message);
		sqlparser_string_free(deparsed);
		sqlparser_handle_destroy(handle);
		return 0;
	}
	sqlparser_string_free(deparsed);
	sqlparser_handle_destroy(handle);
	return 1;
}

static int mutation_marker_lifecycle(void)
{
	sqlparser_handle_t *clone;
	sqlparser_handle_t *handle;
	sqlparser_handle_t *reparsed;
	sqlparser_name_view_t name;
	sqlparser_error_t error;
	char *deparsed;
	size_t count;
	size_t index;
	size_t target_index;

	handle = NULL;
	clone = NULL;
	reparsed = NULL;
	deparsed = NULL;
	target_index = (size_t)-1;
	memset(&error, 0, sizeof(error));
	if (sqlparser_parse(
		    "SELECT Foo AS OldAlias FROM MixedTable",
		    &handle,
		    &error) != SQLPARSER_STATUS_OK ||
	    sqlparser_statement_name_count(
		    handle,
		    0U,
		    &count,
		    &error) != SQLPARSER_STATUS_OK) {
		fprintf(stderr, "FAIL: mutation marker lifecycle setup failed\n");
		sqlparser_handle_destroy(handle);
		return 0;
	}
	for (index = 0U; index < count; index++) {
		if (sqlparser_statement_name(
			    handle,
			    0U,
			    index,
			    &name,
			    &error) == SQLPARSER_STATUS_OK &&
		    name.owner_type != NULL &&
		    name.field_name != NULL &&
		    name.value != NULL &&
		    strcmp(name.owner_type, "ResTarget") == 0 &&
		    strcmp(name.field_name, "name") == 0 &&
		    strcmp(name.value, "OldAlias") == 0) {
			target_index = index;
			break;
		}
	}
	if (target_index == (size_t)-1 ||
	    sqlparser_statement_set_name(
		    handle,
		    0U,
		    target_index,
		    "SELECT",
		    &error) != SQLPARSER_STATUS_OK ||
	    handle->identifier_mutation_count != 1U ||
	    sqlparser_handle_clone(handle, &clone, &error) !=
		    SQLPARSER_STATUS_OK ||
	    sqlparser_deparse(clone, &deparsed, &error) !=
		    SQLPARSER_STATUS_OK ||
	    deparsed == NULL ||
	    strstr(deparsed, "Foo AS \"SELECT\" FROM MixedTable") == NULL) {
		fprintf(
			stderr,
			"FAIL: cloned identifier mutation lost provenance: %s\n",
			deparsed != NULL ? deparsed : error.message);
		sqlparser_string_free(deparsed);
		sqlparser_handle_destroy(clone);
		sqlparser_handle_destroy(handle);
		return 0;
	}
	sqlparser_string_free(deparsed);
	deparsed = NULL;
	sqlparser_handle_destroy(clone);
	clone = NULL;
	if (sqlparser_select_insert_target_sql(
		    handle,
		    0U,
		    0U,
		    0U,
		    "Added",
		    &error) != SQLPARSER_STATUS_OK ||
	    sqlparser_deparse(handle, &deparsed, &error) !=
		    SQLPARSER_STATUS_OK ||
	    deparsed == NULL ||
	    strstr(
		    deparsed,
		    "SELECT added, Foo AS \"SELECT\" FROM MixedTable") ==
		    NULL) {
		fprintf(
			stderr,
			"FAIL: structural mutation lost identifier provenance: %s\n",
			deparsed != NULL ? deparsed : error.message);
		sqlparser_string_free(deparsed);
		sqlparser_handle_destroy(handle);
		return 0;
	}
	sqlparser_string_free(deparsed);
	deparsed = NULL;
	target_index = (size_t)-1;
	if (sqlparser_statement_name_count(
		    handle,
		    0U,
		    &count,
		    &error) != SQLPARSER_STATUS_OK) {
		sqlparser_handle_destroy(handle);
		return 0;
	}
	for (index = 0U; index < count; index++) {
		if (sqlparser_statement_name(
			    handle,
			    0U,
			    index,
			    &name,
			    &error) == SQLPARSER_STATUS_OK &&
		    name.owner_type != NULL &&
		    name.field_name != NULL &&
		    name.value != NULL &&
		    strcmp(name.owner_type, "ResTarget") == 0 &&
		    strcmp(name.field_name, "name") == 0 &&
		    strcmp(name.value, "SELECT") == 0) {
			target_index = index;
			break;
		}
	}
	if (target_index == (size_t)-1 ||
	    sqlparser_statement_set_name(
		    handle,
		    0U,
		    target_index,
		    "OldAlias",
		    &error) != SQLPARSER_STATUS_OK ||
	    handle->identifier_mutation_count != 0U ||
	    sqlparser_deparse(handle, &deparsed, &error) !=
		    SQLPARSER_STATUS_OK ||
	    deparsed == NULL ||
	    strstr(
		    deparsed,
		    "SELECT added, Foo AS OldAlias FROM MixedTable") ==
		    NULL ||
	    sqlparser_parse(deparsed, &reparsed, &error) !=
		    SQLPARSER_STATUS_OK) {
		fprintf(
			stderr,
			"FAIL: restoring an identifier did not clear provenance: %s\n",
			deparsed != NULL ? deparsed : error.message);
		sqlparser_handle_destroy(reparsed);
		sqlparser_string_free(deparsed);
		sqlparser_handle_destroy(handle);
		return 0;
	}
	sqlparser_handle_destroy(reparsed);
	sqlparser_string_free(deparsed);
	sqlparser_handle_destroy(handle);
	return 1;
}

static int failed_commit_preserves_mutation_provenance(void)
{
	sqlparser_handle_t *handle;
	sqlparser_error_t error;
	sqlparser_limits_t limits;
	char *deparsed;
	size_t target_index;
	sqlparser_status_t status;

	handle = NULL;
	deparsed = NULL;
	memset(&error, 0, sizeof(error));
	if (sqlparser_parse(
		    "SELECT Foo AS OldAlias FROM MixedTable; SELECT 1",
		    &handle,
		    &error) != SQLPARSER_STATUS_OK ||
	    !statement_find_name_index(
		    handle,
		    0U,
		    "ResTarget",
		    "name",
		    "OldAlias",
		    &target_index) ||
	    sqlparser_statement_set_name(
		    handle,
		    0U,
		    target_index,
		    "SELECT",
		    &error) != SQLPARSER_STATUS_OK ||
	    handle->identifier_mutation_count != 1U) {
		fprintf(
			stderr,
			"FAIL: failed-commit provenance setup failed: %s\n",
			error.message);
		sqlparser_handle_destroy(handle);
		return 0;
	}
	limits = handle->limits;
	handle->limits.max_statement_count = 1U;
	status = sqlparser_select_insert_target_sql(
		handle,
		0U,
		0U,
		0U,
		"Added",
		&error);
	handle->limits = limits;
	if (status != SQLPARSER_STATUS_RESOURCE_LIMIT ||
	    handle->identifier_mutation_count != 1U ||
	    sqlparser_deparse(handle, &deparsed, &error) !=
		    SQLPARSER_STATUS_OK ||
	    deparsed == NULL ||
	    strstr(
		    deparsed,
		    "SELECT Foo AS \"SELECT\" FROM MixedTable") == NULL ||
	    strstr(deparsed, "Added") != NULL) {
		fprintf(
			stderr,
			"FAIL: failed commit changed identifier provenance: %s\n",
			deparsed != NULL ? deparsed : error.message);
		sqlparser_string_free(deparsed);
		sqlparser_handle_destroy(handle);
		return 0;
	}
	sqlparser_string_free(deparsed);
	sqlparser_handle_destroy(handle);
	return 1;
}

static int relation_group_preserves_source_spelling(void)
{
	sqlparser_handle_t *handle;
	sqlparser_error_t error;
	char *deparsed;

	handle = NULL;
	deparsed = NULL;
	memset(&error, 0, sizeof(error));
	if (sqlparser_parse(
		    "SELECT * FROM MixedSchema.MixedTable",
		    &handle,
		    &error) != SQLPARSER_STATUS_OK ||
	    sqlparser_statement_set_relation_name(
		    handle,
		    0U,
		    0U,
		    NULL,
		    "MixedTable",
		    &error) != SQLPARSER_STATUS_OK ||
	    handle->identifier_mutation_count != 3U ||
	    sqlparser_deparse(handle, &deparsed, &error) !=
		    SQLPARSER_STATUS_OK ||
	    deparsed == NULL ||
	    strstr(deparsed, "FROM MixedTable") == NULL ||
	    strstr(deparsed, "\"MixedTable\"") != NULL) {
		fprintf(
			stderr,
			"FAIL: removing a relation qualifier changed table spelling: %s\n",
			deparsed != NULL ? deparsed : error.message);
		sqlparser_string_free(deparsed);
		sqlparser_handle_destroy(handle);
		return 0;
	}
	sqlparser_string_free(deparsed);
	deparsed = NULL;
	if (sqlparser_statement_set_relation_name(
		    handle,
		    0U,
		    0U,
		    "MixedSchema",
		    "MixedTable",
		    &error) != SQLPARSER_STATUS_OK ||
	    handle->identifier_mutation_count != 0U ||
	    sqlparser_deparse(handle, &deparsed, &error) !=
		    SQLPARSER_STATUS_OK ||
	    deparsed == NULL ||
	    strstr(
		    deparsed,
		    "FROM MixedSchema.MixedTable") == NULL) {
		fprintf(
			stderr,
			"FAIL: restoring a relation did not clear group provenance: %s\n",
			deparsed != NULL ? deparsed : error.message);
		sqlparser_string_free(deparsed);
		sqlparser_handle_destroy(handle);
		return 0;
	}
	sqlparser_string_free(deparsed);
	sqlparser_handle_destroy(handle);

	handle = NULL;
	deparsed = NULL;
	if (sqlparser_parse(
		    "SELECT * FROM MixedTable",
		    &handle,
		    &error) != SQLPARSER_STATUS_OK ||
	    sqlparser_statement_set_relation_name(
		    handle,
		    0U,
		    0U,
		    "new_schema",
		    "MixedTable",
		    &error) != SQLPARSER_STATUS_OK ||
	    sqlparser_deparse(handle, &deparsed, &error) !=
		    SQLPARSER_STATUS_OK ||
	    deparsed == NULL ||
	    strstr(
		    deparsed,
		    "FROM new_schema.MixedTable") == NULL ||
	    strstr(deparsed, "\"MixedTable\"") != NULL) {
		fprintf(
			stderr,
			"FAIL: adding a relation qualifier changed table spelling: %s\n",
			deparsed != NULL ? deparsed : error.message);
		sqlparser_string_free(deparsed);
		sqlparser_handle_destroy(handle);
		return 0;
	}
	sqlparser_string_free(deparsed);
	sqlparser_handle_destroy(handle);
	return 1;
}

static int generated_identifier_has_no_source_provenance(void)
{
	sqlparser_handle_t *handle;
	sqlparser_error_t error;
	char *deparsed;
	size_t target_index;

	handle = NULL;
	deparsed = NULL;
	memset(&error, 0, sizeof(error));
	if (sqlparser_parse(
		    "SELECT Existing FROM SourceTable",
		    &handle,
		    &error) != SQLPARSER_STATUS_OK ||
	    sqlparser_select_insert_target_sql(
		    handle,
		    0U,
		    0U,
		    0U,
		    "\"Existing\"",
		    &error) != SQLPARSER_STATUS_OK ||
	    !statement_find_name_index(
		    handle,
		    0U,
		    "ColumnRef",
		    "fields",
		    "Existing",
		    &target_index) ||
	    sqlparser_statement_set_name(
		    handle,
		    0U,
		    target_index,
		    "changed",
		    &error) != SQLPARSER_STATUS_OK ||
	    handle->identifier_mutation_count != 1U ||
	    handle->identifier_mutations[0].source_present ||
	    sqlparser_deparse(handle, &deparsed, &error) !=
		    SQLPARSER_STATUS_OK ||
	    deparsed == NULL ||
	    strstr(
		    deparsed,
		    "SELECT changed, Existing FROM SourceTable") == NULL) {
		fprintf(
			stderr,
			"FAIL: generated identifier borrowed source provenance: %s\n",
			deparsed != NULL ? deparsed : error.message);
		sqlparser_string_free(deparsed);
		sqlparser_handle_destroy(handle);
		return 0;
	}
	sqlparser_string_free(deparsed);
	sqlparser_handle_destroy(handle);
	return 1;
}

static int identifier_role_classifier_is_fail_closed(void)
{
	static const struct {
		const char *sql;
		const char *owner_type;
		const char *field_name;
		const char *value;
	} denied[] = {
		{
			"CREATE ROLE r SUPERUSER",
			"DefElem",
			"defname",
			"superuser"
		},
		{
			"CREATE FUNCTION f() RETURNS integer LANGUAGE sql "
			"IMMUTABLE AS 'SELECT 1'",
			"DefElem",
			"arg",
			"immutable"
		},
		{
			"SELECT 1 IN (1)",
			"AExpr",
			"name",
			"="
		},
		{
			"SET TRANSACTION ISOLATION LEVEL SERIALIZABLE",
			"VariableSetStmt",
			"name",
			"TRANSACTION"
		},
		{
			"CREATE POLICY p ON t FOR SELECT USING (true)",
			"CreatePolicyStmt",
			"cmd_name",
			"select"
		},
		{
			"SELECT 'a' SIMILAR TO 'a'",
			"FuncCall",
			"funcname",
			"similar_to_escape"
		},
		{
			"SELECT 'a' LIKE 'a' ESCAPE '!'",
			"FuncCall",
			"funcname",
			"pg_catalog"
		},
		{
			"SELECT 'a' LIKE 'a' ESCAPE '!'",
			"FuncCall",
			"funcname",
			"like_escape"
		},
		{
			"CREATE TEMP TABLE t(a int)",
			"RangeVar",
			"relpersistence",
			"t"
		},
		{
			"CREATE TYPE mood AS ENUM ('sad')",
			"CreateEnumStmt",
			"vals",
			"sad"
		},
		{
			"CREATE TRIGGER tr BEFORE INSERT ON t "
			"EXECUTE FUNCTION f('arg')",
			"CreateTrigStmt",
			"args",
			"arg"
		},
		{
			"COMMENT ON TABLE t IS 'comment'",
			"CommentStmt",
			"comment",
			"comment"
		},
		{
			"NOTIFY c, 'payload'",
			"NotifyStmt",
			"payload",
			"payload"
		}
	};
	static const expected_name_t explicit_function_schema = {
		"FuncCall",
		"funcname",
		"public"
	};
	static const expected_name_t explicit_function_name = {
		"FuncCall",
		"funcname",
		"MixedFn"
	};
	sqlparser_handle_t *handle;
	sqlparser_error_t error;
	char *deparsed;
	size_t index;
	size_t option_index;

	for (index = 0U; index < sizeof(denied) / sizeof(denied[0]); index++) {
		handle = NULL;
		memset(&error, 0, sizeof(error));
		if (sqlparser_parse(denied[index].sql, &handle, &error) !=
		    SQLPARSER_STATUS_OK) {
			fprintf(
				stderr,
				"FAIL: identifier role case did not parse: %s: %s\n",
				denied[index].sql,
				error.message);
			return 0;
		}
		if (statement_find_name_index(
			    handle,
			    0U,
			    denied[index].owner_type,
			    denied[index].field_name,
			    denied[index].value,
			    &option_index)) {
			fprintf(
				stderr,
				"FAIL: semantic string was exposed as name: %s.%s=%s\n",
				denied[index].owner_type,
				denied[index].field_name,
				denied[index].value);
			sqlparser_handle_destroy(handle);
			return 0;
		}
		sqlparser_handle_destroy(handle);
	}

	handle = NULL;
	memset(&error, 0, sizeof(error));
	if (sqlparser_parse(
		    "SELECT public.\"MixedFn\"(1)",
		    &handle,
		    &error) != SQLPARSER_STATUS_OK ||
	    !statement_has_name(handle, 0U, &explicit_function_schema) ||
	    !statement_has_name(handle, 0U, &explicit_function_name)) {
		fprintf(
			stderr,
			"FAIL: explicit function identifiers were not exposed: %s\n",
			error.message);
		sqlparser_handle_destroy(handle);
		return 0;
	}
	sqlparser_handle_destroy(handle);

	handle = NULL;
	deparsed = NULL;
	option_index = (size_t)-1;
	memset(&error, 0, sizeof(error));
	if (sqlparser_parse(
		    "CREATE FOREIGN TABLE ft(a int) SERVER s "
		    "OPTIONS (\"RemoteKey\" 'v')",
		    &handle,
		    &error) != SQLPARSER_STATUS_OK ||
	    !statement_find_name_index(
		    handle,
		    0U,
		    "DefElem",
		    "defname",
		    "RemoteKey",
		    &option_index) ||
	    sqlparser_statement_set_name(
		    handle,
		    0U,
		    option_index,
		    "NewKey",
		    &error) != SQLPARSER_STATUS_OK ||
	    sqlparser_deparse(handle, &deparsed, &error) !=
		    SQLPARSER_STATUS_OK ||
	    deparsed == NULL ||
	    strstr(deparsed, "NewKey") == NULL) {
		fprintf(
			stderr,
			"FAIL: generic option identifier was not safely mutable: %s\n",
			deparsed != NULL ? deparsed : error.message);
		sqlparser_string_free(deparsed);
		sqlparser_handle_destroy(handle);
		return 0;
	}
	sqlparser_string_free(deparsed);
	sqlparser_handle_destroy(handle);
	return 1;
}

static int graph_name_selector_uses_public_name_ordinal(void)
{
	sqlparser_graph_field_t field;
	sqlparser_handle_t *handle;
	sqlparser_query_graph_view_t graph;
	sqlparser_error_t error;
	char *deparsed;
	size_t field_index;
	int found;

	handle = NULL;
	deparsed = NULL;
	found = 0;
	memset(&error, 0, sizeof(error));
	if (sqlparser_parse(
		    "SELECT 'a' LIKE 'a' ESCAPE '!', Wanted FROM t",
		    &handle,
		    &error) != SQLPARSER_STATUS_OK ||
	    sqlparser_statement_query_graph(
		    handle,
		    0U,
		    &graph,
		    &error) != SQLPARSER_STATUS_OK) {
		fprintf(
			stderr,
			"FAIL: graph selector alignment setup failed: %s\n",
			error.message);
		sqlparser_handle_destroy(handle);
		return 0;
	}
	for (field_index = 0U; field_index < graph.field_count; field_index++) {
		memset(&field, 0, sizeof(field));
		if (sqlparser_query_graph_field_at(
			    &graph,
			    field_index,
			    &field,
			    &error) == SQLPARSER_STATUS_OK &&
		    field.column_name != NULL &&
		    strcmp(field.column_name, "Wanted") == 0 &&
		    field.has_selector) {
			found = 1;
			break;
		}
	}
	if (!found ||
	    sqlparser_selector_set_name(
		    handle,
		    &field.selector,
		    "Changed",
		    &error) != SQLPARSER_STATUS_OK ||
	    sqlparser_deparse(handle, &deparsed, &error) !=
		    SQLPARSER_STATUS_OK ||
	    deparsed == NULL ||
	    strstr(deparsed, "ESCAPE '!'") == NULL ||
	    strstr(deparsed, "Changed") == NULL ||
	    strstr(deparsed, "FROM t") == NULL ||
	    strstr(deparsed, "Wanted") != NULL) {
		fprintf(
			stderr,
			"FAIL: graph name selector ordinal was not aligned: %s\n",
			deparsed != NULL ? deparsed : error.message);
		sqlparser_string_free(deparsed);
		sqlparser_handle_destroy(handle);
		return 0;
	}
	sqlparser_string_free(deparsed);
	sqlparser_handle_destroy(handle);
	return 1;
}

static int unconsumed_identifier_tag_fails_closed(void)
{
	sqlparser_handle_t *handle;
	sqlparser_error_t error;
	PgQuery__Node *statement;
	PgQuery__NotifyStmt *notify_stmt;
	char *deparsed;
	size_t mutation_index;
	size_t target_index;
	int mutation_created;
	sqlparser_status_t status;

	handle = NULL;
	deparsed = NULL;
	statement = NULL;
	notify_stmt = NULL;
	mutation_index = 0U;
	mutation_created = 0;
	memset(&error, 0, sizeof(error));
	if (sqlparser_parse(
		    "NOTIFY MixedChannel, 'PayloadCase'",
		    &handle,
		    &error) != SQLPARSER_STATUS_OK) {
		fprintf(
			stderr,
			"FAIL: unconsumed tag parse failed: %s\n",
			error.message);
		return 0;
	}
	if (statement_find_name_index(
		    handle,
		    0U,
		    "NotifyStmt",
		    "payload",
		    "PayloadCase",
		    &target_index)) {
		fprintf(stderr, "FAIL: NOTIFY payload was exposed as a name\n");
		sqlparser_handle_destroy(handle);
		return 0;
	}
	if (sqlparser_get_statement_node(
		    handle,
		    0U,
		    &statement,
		    &error) != SQLPARSER_STATUS_OK ||
	    statement == NULL ||
	    statement->notify_stmt == NULL) {
		fprintf(
			stderr,
			"FAIL: unconsumed tag statement lookup failed: %s\n",
			error.message);
		sqlparser_handle_destroy(handle);
		return 0;
	}
	notify_stmt = statement->notify_stmt;
	if (sqlparser_handle_prepare_identifier_mutation(
		    handle,
		    0U,
		    &notify_stmt->payload,
		    (ProtobufCMessage *)notify_stmt,
		    &mutation_index,
		    &mutation_created,
		    &error) != SQLPARSER_STATUS_OK ||
	    !mutation_created ||
	    sqlparser_replace_proto_string(
		    &notify_stmt->payload,
		    "ChangedPayload",
		    &error) != SQLPARSER_STATUS_OK) {
		fprintf(
			stderr,
			"FAIL: unconsumed tag setup failed: %s\n",
			error.message);
		sqlparser_handle_destroy(handle);
		return 0;
	}
	handle->identifier_mutations[mutation_index].slot =
		&notify_stmt->payload;
	handle->identifier_mutations[mutation_index].value =
		notify_stmt->payload;
	if (sqlparser_handle_commit_ast(handle, &error) !=
	    SQLPARSER_STATUS_OK) {
		fprintf(
			stderr,
			"FAIL: unconsumed tag commit failed: %s\n",
			error.message);
		sqlparser_handle_destroy(handle);
		return 0;
	}
	status = sqlparser_deparse(handle, &deparsed, &error);
	if (status != SQLPARSER_STATUS_INTERNAL_ERROR ||
	    deparsed != NULL) {
		fprintf(
			stderr,
			"FAIL: unconsumed identifier tag did not fail closed\n");
		sqlparser_string_free(deparsed);
		sqlparser_handle_destroy(handle);
		return 0;
	}
	sqlparser_handle_destroy(handle);
	return 1;
}

int main(void)
{
	static const char long_name[] =
		"AbCdEfGhIjKlMnOpQrStUvWxYz0123456789_"
		"AbCdEfGhIjKlMnOpQrStUvWxYz0123456789";
	static const char shared_prefix[] =
		"AbCdEfGhIjKlMnOpQrStUvWxYz0123456789_"
		"AbCdEfGhIjKlMnOpQrStUvWxYz";
	static const char shared_name_a[] =
		"AbCdEfGhIjKlMnOpQrStUvWxYz0123456789_"
		"AbCdEfGhIjKlMnOpQrStUvWxYzA";
	static const char shared_name_b[] =
		"AbCdEfGhIjKlMnOpQrStUvWxYz0123456789_"
		"AbCdEfGhIjKlMnOpQrStUvWxYzB";
	static const spelling_case_t cases[] = {
		{
			SQLPARSER_DIALECT_POSTGRESQL,
			"select abc AS MixedAlias from SchemaCase.DDD",
			0U,
			{
				{"ColumnRef", "fields", "abc"},
				{"ResTarget", "name", "MixedAlias"},
				{"RangeVar", "schemaname", "SchemaCase"},
				{"RangeVar", "relname", "DDD"}
			},
			4U
		},
		{
			SQLPARSER_DIALECT_POSTGRESQL,
			"select firstCol from FirstTable; SELECT SecondCol FROM SecondTable",
			1U,
			{
				{"ColumnRef", "fields", "SecondCol"},
				{"RangeVar", "relname", "SecondTable"}
			},
			2U
		},
		{
			SQLPARSER_DIALECT_POSTGRESQL,
			"WITH MixedCTE AS (SELECT SrcCol FROM BaseTable) "
			"SELECT MixedCTE.SrcCol FROM MixedCTE",
			0U,
			{
				{"CommonTableExpr", "ctename", "MixedCTE"},
				{"RangeVar", "relname", "BaseTable"},
				{"RangeVar", "relname", "MixedCTE"},
				{"ColumnRef", "fields", "SrcCol"}
			},
			4U
		},
		{
			SQLPARSER_DIALECT_MYSQL,
			"SELECT `MiX``Name` AS `AliasCase` FROM `DbCase`.`TableCase`",
			0U,
			{
				{"ColumnRef", "fields", "MiX`Name"},
				{"ResTarget", "name", "AliasCase"},
				{"RangeVar", "schemaname", "DbCase"},
				{"RangeVar", "relname", "TableCase"}
			},
			4U
		},
		{
			SQLPARSER_DIALECT_SQLSERVER,
			"SELECT [MiX]]Name] AS [AliasCase] FROM [DbCase].[dbo].[TableCase]",
			0U,
			{
				{"ColumnRef", "fields", "MiX]Name"},
				{"ResTarget", "name", "AliasCase"},
				{"RangeVar", "catalogname", "DbCase"},
				{"RangeVar", "schemaname", "dbo"},
				{"RangeVar", "relname", "TableCase"}
			},
			5U
		},
		{
			SQLPARSER_DIALECT_POSTGRESQL,
			"SELECT \"MiX\"\"Name\" FROM \"SchemaCase\".\"TableCase\"",
			0U,
			{
				{"ColumnRef", "fields", "MiX\"Name"},
				{"RangeVar", "schemaname", "SchemaCase"},
				{"RangeVar", "relname", "TableCase"}
			},
			3U
		},
		{
			SQLPARSER_DIALECT_POSTGRESQL,
			"SELECT U&\"MiX\" FROM U&\"TableCase\"",
			0U,
			{
				{"ColumnRef", "fields", "MiX"},
				{"RangeVar", "relname", "TableCase"}
			},
			2U
		},
		{
			SQLPARSER_DIALECT_POSTGRESQL,
			"SELECT "
			"AbCdEfGhIjKlMnOpQrStUvWxYz0123456789_"
			"AbCdEfGhIjKlMnOpQrStUvWxYz0123456789 FROM "
			"AbCdEfGhIjKlMnOpQrStUvWxYz0123456789_"
			"AbCdEfGhIjKlMnOpQrStUvWxYz0123456789",
			0U,
			{
				{"ColumnRef", "fields", long_name},
				{"RangeVar", "relname", long_name}
			},
			2U
		},
		{
			SQLPARSER_DIALECT_POSTGRESQL,
			"PREPARE MiXeDPlan AS SELECT 1",
			0U,
			{
				{"PrepareStmt", "name", "MiXeDPlan"}
			},
			1U
		},
		{
			SQLPARSER_DIALECT_POSTGRESQL,
			"CREATE ROLE MiXeDRole",
			0U,
			{
				{"CreateRoleStmt", "role", "MiXeDRole"}
			},
			1U
		},
		{
			SQLPARSER_DIALECT_POSTGRESQL,
			"CREATE ROLE MiXeDRole SUPERUSER LOGIN NOINHERIT",
			0U,
			{
				{"CreateRoleStmt", "role", "MiXeDRole"}
			},
			1U
		},
		{
			SQLPARSER_DIALECT_POSTGRESQL,
			"CREATE POLICY PolicyCase ON TableCase AS PERMISSIVE",
			0U,
			{
				{"CreatePolicyStmt", "policy_name", "PolicyCase"},
				{"RangeVar", "relname", "TableCase"}
			},
			2U
		},
		{
			SQLPARSER_DIALECT_POSTGRESQL,
			"GRANT RoleCase TO PUBLIC",
			0U,
			{{NULL, NULL, NULL}},
			0U
		},
		{
			SQLPARSER_DIALECT_POSTGRESQL,
			"INSERT INTO TargetTable VALUES (1) "
			"ON CONFLICT ON CONSTRAINT MiXeDConstraint DO NOTHING",
			0U,
			{
				{"RangeVar", "relname", "TargetTable"},
				{"InferClause", "conname", "MiXeDConstraint"}
			},
			2U
		},
		{
			SQLPARSER_DIALECT_POSTGRESQL,
			"UPDATE TargetTable SET TargetColumn = 1 "
			"WHERE CURRENT OF MiXeDCursor",
			0U,
			{
				{"RangeVar", "relname", "TargetTable"},
				{"ResTarget", "name", "TargetColumn"},
				{"CurrentOfExpr", "cursor_name", "MiXeDCursor"}
			},
			3U
		},
		{
			SQLPARSER_DIALECT_POSTGRESQL,
			"SELECT CURRENT_USER AS Current_User",
			0U,
			{
				{"ResTarget", "name", "Current_User"}
			},
			1U
		},
		{
			SQLPARSER_DIALECT_POSTGRESQL,
			"CREATE ROLE CamelRole; "
			"CREATE INDEX CAMELROLE ON CamelRole(CamelRole)",
			0U,
			{
				{"CreateRoleStmt", "role", "CamelRole"}
			},
			1U
		},
		{
			SQLPARSER_DIALECT_POSTGRESQL,
			"CREATE ROLE CamelRole; "
			"CREATE INDEX CAMELROLE ON CamelRole(CamelRole)",
			1U,
			{
				{"IndexStmt", "idxname", "CAMELROLE"},
				{"RangeVar", "relname", "CamelRole"},
				{"IndexElem", "name", "CamelRole"}
			},
			3U
		},
		{
			SQLPARSER_DIALECT_POSTGRESQL,
			"SELECT U&\"Mi\\0058ed\"",
			0U,
			{
				{"ColumnRef", "fields", "MiXed"}
			},
			1U
		},
		{
			SQLPARSER_DIALECT_POSTGRESQL,
			"SELECT "
			"AbCdEfGhIjKlMnOpQrStUvWxYz0123456789_"
			"AbCdEfGhIjKlMnOpQrStUvWxYz, "
			"AbCdEfGhIjKlMnOpQrStUvWxYz0123456789_"
			"AbCdEfGhIjKlMnOpQrStUvWxYzA, "
			"AbCdEfGhIjKlMnOpQrStUvWxYz0123456789_"
			"AbCdEfGhIjKlMnOpQrStUvWxYzB",
			0U,
			{
				{"ColumnRef", "fields", shared_prefix},
				{"ColumnRef", "fields", shared_name_a},
				{"ColumnRef", "fields", shared_name_b}
			},
			3U
		},
		{
			SQLPARSER_DIALECT_POSTGRESQL,
			"SELECT \""
			"AbCdEfGhIjKlMnOpQrStUvWxYz0123456789_"
			"AbCdEfGhIjKlMnOpQrStUvWxYz0123456789\"",
			0U,
			{
				{"ColumnRef", "fields", long_name}
			},
			1U
		},
		{
			SQLPARSER_DIALECT_POSTGRESQL,
			"SELECT U&\""
			"AbCdEfGhIjKlMnOpQrStUvWxYz0123456789_"
			"AbCdEfGhIjKlMnOpQrStUvWxYz0123456789\"",
			0U,
			{
				{"ColumnRef", "fields", long_name}
			},
			1U
		}
	};
	sqlparser_parse_options_t options;
	sqlparser_handle_t *handle;
	sqlparser_error_t error;
	size_t case_index;
	size_t name_index;

	for (case_index = 0U;
	     case_index < sizeof(cases) / sizeof(cases[0]);
	     case_index++) {
		handle = NULL;
		memset(&error, 0, sizeof(error));
		sqlparser_parse_options_default(&options);
		options.dialect = cases[case_index].dialect;
		if (sqlparser_parse_with_options(
			    cases[case_index].sql,
			    &options,
			    &handle,
			    &error) != SQLPARSER_STATUS_OK) {
			fprintf(
				stderr,
				"FAIL: identifier spelling case %lu did not parse: %s\n",
				(unsigned long)case_index,
				error.message);
			return 1;
		}
		if (sqlparser_validate_ast_identifier_spelling(handle, &error) !=
		    SQLPARSER_STATUS_OK) {
			fprintf(
				stderr,
				"FAIL: identifier spelling audit case %lu failed: %s\n",
				(unsigned long)case_index,
				error.message);
			sqlparser_handle_destroy(handle);
			return 1;
		}
		for (name_index = 0U;
		     name_index < cases[case_index].name_count;
		     name_index++) {
			if (!statement_has_name(
				    handle,
				    cases[case_index].statement_index,
				    &cases[case_index].names[name_index])) {
				fprintf(
					stderr,
					"FAIL: identifier spelling case %lu lost %s.%s=%s\n",
					(unsigned long)case_index,
					cases[case_index].names[name_index].owner_type,
					cases[case_index].names[name_index].field_name,
					cases[case_index].names[name_index].value);
				sqlparser_handle_destroy(handle);
				return 1;
			}
		}
		sqlparser_handle_destroy(handle);
	}
	if (!standard_parse_has_spelling(
		    "SELECT MiXeDIdentifier",
		    "\"sval\":\"mixedidentifier\"",
		    "\"sval\":\"MiXeDIdentifier\"") ||
	    !standard_parse_has_spelling(
		    "SELECT "
		    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
		    "\"sval\":\""
		    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\"",
		    "\"sval\":\""
		    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\"") ||
	    !standard_parse_has_spelling(
		    "SELECT \""
		    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\"",
		    "\"sval\":\""
		    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\"",
		    "\"sval\":\""
		    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\"") ||
		    !standard_parse_has_spelling(
			    "SELECT U&\""
			    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\"",
		    "\"sval\":\""
		    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\"",
			    "\"sval\":\""
			    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\"") ||
		    !standard_protobuf_deparse_has_spelling(
			    "SELECT MiXeDIdentifier",
			    "mixedidentifier",
			    "MiXeDIdentifier") ||
		    !standard_protobuf_deparse_has_spelling(
			    "SELECT "
			    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
			    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
			    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa") ||
		    !standard_fingerprints_match(
			    "SELECT MiXeDIdentifier",
			    "SELECT mixedidentifier") ||
	    !standard_fingerprints_match(
		    "SELECT "
		    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
		    "SELECT "
		    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa") ||
	    !xmltable_internal_option_semantics_match()) {
			return 1;
		}
	if (!sqlparser_parse_is_rejected(
		    "CREATE ROLE MiXeDRole \"SUPERUSER\"") ||
	    !sqlparser_parse_is_rejected(
		    "CREATE ROLE MiXeDRole \"superuser\"") ||
	    !sqlparser_parse_is_rejected(
		    "CREATE POLICY PolicyCase ON TableCase AS \"PERMISSIVE\"")) {
		fprintf(
			stderr,
			"FAIL: quoted grammar-only identifiers were accepted\n");
		return 1;
	}

	handle = NULL;
	memset(&error, 0, sizeof(error));
	if (sqlparser_parse("GRANT RoleCase TO PUBLIC", &handle, &error) !=
	    SQLPARSER_STATUS_OK) {
		fprintf(stderr, "FAIL: unquoted PUBLIC did not parse: %s\n", error.message);
		return 1;
	}
	{
		static const expected_name_t public_role = {
			"RoleSpec",
			"rolename",
			"PUBLIC"
		};

		if (statement_has_name(handle, 0U, &public_role)) {
			fprintf(stderr, "FAIL: unquoted PUBLIC became a CSTRING role\n");
			sqlparser_handle_destroy(handle);
			return 1;
		}
	}
	sqlparser_handle_destroy(handle);

	handle = NULL;
	memset(&error, 0, sizeof(error));
	if (sqlparser_parse("GRANT RoleCase TO \"PUBLIC\"", &handle, &error) !=
	    SQLPARSER_STATUS_OK) {
		fprintf(stderr, "FAIL: quoted PUBLIC did not parse: %s\n", error.message);
		return 1;
	}
	{
		static const expected_name_t quoted_public_role = {
			"RoleSpec",
			"rolename",
			"PUBLIC"
		};

		if (!statement_has_name(handle, 0U, &quoted_public_role)) {
			fprintf(stderr, "FAIL: quoted PUBLIC did not remain a CSTRING role\n");
			sqlparser_handle_destroy(handle);
			return 1;
		}
	}
	sqlparser_handle_destroy(handle);

	handle = NULL;
	memset(&error, 0, sizeof(error));
	if (sqlparser_parse("GRANT RoleCase TO \"public\"", &handle, &error) !=
	    SQLPARSER_STATUS_OK) {
		fprintf(stderr, "FAIL: quoted lowercase public did not parse: %s\n", error.message);
		return 1;
	}
	{
		static const expected_name_t quoted_lower_public_role = {
			"RoleSpec",
			"rolename",
			"public"
		};

		if (!statement_has_name(handle, 0U, &quoted_lower_public_role)) {
			fprintf(stderr, "FAIL: quoted lowercase public did not remain a CSTRING role\n");
			sqlparser_handle_destroy(handle);
			return 1;
		}
	}
	sqlparser_handle_destroy(handle);
	if (!query_graph_identifier_semantics() ||
	    !session_identifier_semantics() ||
	    !identifier_role_classifier_is_fail_closed() ||
	    !graph_name_selector_uses_public_name_ordinal() ||
	    !mutation_marker_lifecycle() ||
	    !failed_commit_preserves_mutation_provenance() ||
	    !relation_group_preserves_source_spelling() ||
	    !generated_identifier_has_no_source_provenance() ||
	    !unconsumed_identifier_tag_fails_closed() ||
	    !mutated_keyword_alias_is_quoted() ||
	    !mutated_type_prefix_is_not_synthetic() ||
	    !mutated_name_preserves_siblings(
		    "SELECT MiXeD.ColCase FROM SchemaCase.TableCase",
		    "ColumnRef",
		    "fields",
		    "MiXeD",
		    "other",
		    "other.ColCase",
		    "other.\"ColCase\"") ||
	    !mutated_name_preserves_siblings(
		    "SELECT * FROM MixedTable AS OldAlias",
		    "Alias",
		    "aliasname",
		    "OldAlias",
		    "new_alias",
		    "MixedTable new_alias",
		    "\"MixedTable\"") ||
	    !mutated_name_preserves_siblings(
		    "SELECT * FROM t AS Foo(Old)",
		    "Alias",
		    "colnames",
		    "Old",
		    "Foo",
		    "Foo(\"Foo\")",
		    NULL) ||
	    !mutated_name_preserves_siblings(
		    "WITH CteName(OldCol, KeepCol) AS "
		    "(SELECT 1, 2) SELECT * FROM CteName",
		    "CommonTableExpr",
		    "aliascolnames",
		    "OldCol",
		    "new_col",
		    "CteName(new_col, KeepCol)",
		    "\"KeepCol\"") ||
	    !mutated_name_preserves_siblings(
		    "SELECT * FROM MixedTable AS OldAlias",
		    "RangeVar",
		    "relname",
		    "MixedTable",
		    "other",
		    "other OldAlias",
		    "\"OldAlias\"") ||
	    !mutated_name_preserves_siblings(
		    "SELECT * FROM (SELECT 1) Foo(KeepCase)",
		    "Alias",
		    "aliasname",
		    "Foo",
		    "SELECT",
		    ") \"SELECT\"(KeepCase)",
		    "\"KeepCase\"") ||
	    !mutated_name_preserves_siblings(
		    "SELECT * FROM (SELECT 1) AS \"select\"(\"select\")",
		    "Alias",
		    "aliasname",
		    "select",
		    "other",
		    ") other(\"select\")",
		    NULL) ||
	    !mutated_name_preserves_siblings(
		    "WITH cte(comment, KeepCase) AS "
		    "(SELECT 1, 2) SELECT * FROM cte",
		    "CommonTableExpr",
		    "aliascolnames",
		    "comment",
		    "new_col",
		    "cte(new_col, KeepCase)",
		    "\"KeepCase\"") ||
	    !mutated_name_preserves_siblings(
		    "SELECT 1::comment.bit",
		    "TypeName",
		    "names",
		    "comment",
		    "pg_catalog",
		    "::pg_catalog.bit",
		    NULL) ||
	    !mutated_name_preserves_siblings(
		    "SET comment TO KeepCase",
		    "VariableSetStmt",
		    "name",
		    "comment",
		    "SET",
		    "SET \"SET\" TO KeepCase",
		    "\"KeepCase\"") ||
	    !mutated_dialect_keeps_unmodified_spelling(
		    SQLPARSER_DIALECT_MYSQL,
		    "SELECT Foo, `Foo` AS `OldAlias` FROM `Foo`",
		    "OldAlias",
		    "NewAlias",
		    "SELECT Foo, `Foo` AS `NewAlias` FROM `Foo`") ||
	    !mutated_dialect_keeps_unmodified_spelling(
		    SQLPARSER_DIALECT_ORACLE,
		    "INSERT ALL INTO TargetTable (SameCase) VALUES (1) "
		    "SELECT \"SameCase\" AS KeepAlias FROM SourceTable",
		    "KeepAlias",
		    "ChangedAlias",
		    "SELECT \"SameCase\" AS")) {
		return 1;
	}
	if (!ast_deparse_preserves(
		    "SELECT row_number() OVER \"AS\" FROM T "
		    "WINDOW \"AS\" AS (ORDER BY Id)",
		    "OVER \"AS\"",
		    "WINDOW \"AS\" AS",
		    "WINDOW AS AS") ||
	    !ast_deparse_preserves_dialect(
		    SQLPARSER_DIALECT_MYSQL,
		    "SELECT CAST(`A` AS bit) FROM pg_catalog.T",
		    "::bit(1)",
		    "FROM pg_catalog.T",
		    "::pg_catalog.bit") ||
	    !ast_deparse_preserves_dialect(
		    SQLPARSER_DIALECT_MYSQL,
		    "SELECT CAST(`A` AS double precision) FROM `precision`",
		    "::double precision",
		    "FROM `precision`",
		    "FROM precision") ||
	    !ast_deparse_preserves_dialect(
		    SQLPARSER_DIALECT_MYSQL,
		    "SELECT CAST(1 AS bit) FROM `t`",
		    "::bit(1)",
		    "FROM `t`",
		    "FROM t") ||
	    !ast_deparse_preserves_dialect(
		    SQLPARSER_DIALECT_MYSQL,
		    "SELECT overlay('abc' PLACING 'x' FROM 1) FROM `t`",
		    "overlay(",
		    "FROM `t`",
		    "pg_catalog.overlay") ||
	    !ast_deparse_preserves_dialect(
		    SQLPARSER_DIALECT_MYSQL,
		    "SELECT CAST(`A` AS timestamp(3) with time zone) "
		    "FROM `zone`",
		    "with time zone",
		    "FROM `zone`",
		    "FROM zone") ||
	    !ast_deparse_preserves(
		    "SET ROLE NONE",
		    "SET role",
		    "NONE",
		    "'none'") ||
	    !ast_deparse_preserves(
		    "create role \"role\"",
		    "CREATE ROLE \"role\"",
		    NULL,
		    "CREATE ROLE role") ||
	    !ast_deparse_preserves(
		    "CREATE TEXT SEARCH DICTIONARY d "
		    "(foo = 'select', language = NONE)",
		    "foo = 'select'",
		    "language = NONE",
		    "foo = select") ||
	    !ast_deparse_preserves(
		    "COPY T TO STDOUT WITH (foo ON, language 'on')",
		    "foo ON",
		    "language 'on'",
		    "foo 'on'") ||
	    !ast_deparse_preserves(
		    "COPY T TO STDOUT WITH (foo (ON, 'on'))",
		    "foo (ON, 'on')",
		    NULL,
		    "foo (on, on)") ||
	    !ast_deparse_preserves(
		    "CREATE INDEX Idx ON T USING \"BtReE\" (C)",
		    "USING \"BtReE\"",
		    "(C)",
		    NULL) ||
	    !ast_deparse_preserves(
		    "SET \"timezone\" TO 'off'",
		    "SET \"timezone\"",
		    "'off'",
		    "TIME ZONE") ||
	    !ast_deparse_preserves(
		    "SET SCHEMA 'CaseSchema'",
		    "'CaseSchema'",
		    NULL,
		    "\"search_path\"") ||
	    !ast_deparse_preserves(
		    "SET search_path TO 'x', Bar",
		    "'x', Bar",
		    NULL,
		    "'Bar'") ||
	    !ast_deparse_preserves(
		    "SHOW \"timezone\"",
		    "SHOW \"timezone\"",
		    NULL,
		    "SHOW TIME ZONE") ||
	    !ast_deparse_preserves(
		    "SELECT C FROM T WHERE C LIKE "
		    "pg_catalog.like_escape($1, $2)",
		    "LIKE pg_catalog.like_escape(",
		    NULL,
		    " ESCAPE ") ||
	    !ast_deparse_preserves(
		    "SELECT pg_catalog.json_object('k', 1)",
		    "pg_catalog.json_object(",
		    NULL,
		    "SELECT JSON_OBJECT(") ||
	    !ast_deparse_preserves(
		    "SELECT pg_catalog.overlay('abcd', 'X', 2, 1)",
		    "pg_catalog.overlay(",
		    NULL,
		    " PLACING ") ||
	    !ast_deparse_preserves(
		    "SELECT B'1'::pg_catalog.bit(1), "
		    "B'1'::pg_catalog.varbit(8)",
		    "::pg_catalog.bit(1)",
		    "::pg_catalog.varbit(8)",
		    NULL) ||
	    !ast_deparse_preserves(
		    "CREATE FUNCTION F() RETURNS int AS "
		    "'BEGIN RETURN 1; END' LANGUAGE PLpgSQL",
		    "LANGUAGE PLpgSQL",
		    NULL,
		    "LANGUAGE 'PLpgSQL'") ||
	    !ast_deparse_preserves(
		    "CREATE FUNCTION FQuoted() RETURNS int AS "
		    "'BEGIN RETURN 1; END' LANGUAGE 'PLpgSQL'",
		    "LANGUAGE 'PLpgSQL'",
		    NULL,
		    NULL) ||
	    !ast_deparse_preserves(
		    "CREATE DATABASE Db ENCODING 'UTF8' TEMPLATE Template0",
		    "ENCODING 'UTF8'",
		    "TEMPLATE Template0",
		    "'Template0'") ||
	    !ast_deparse_preserves(
		    "CREATE DATABASE Db ENCODING E'UT\\'F8' TEMPLATE Template0",
		    "ENCODING 'UT''F8'",
		    "TEMPLATE Template0",
		    "'Template0'") ||
	    !ast_deparse_preserves(
		    "CREATE DATABASE DbQuoted ALLOW_CONNECTIONS 'on'",
		    "ALLOW_CONNECTIONS 'on'",
		    NULL,
		    "ALLOW_CONNECTIONS ON") ||
	    !ast_deparse_preserves(
		    "CREATE DATABASE Db ALLOW_CONNECTIONS on IS_TEMPLATE 'on'",
		    "ALLOW_CONNECTIONS on",
		    "IS_TEMPLATE 'on'",
		    "IS_TEMPLATE ON") ||
	    !ast_deparse_preserves(
		    "SET TIME ZONE timezone",
		    "TO timezone",
		    NULL,
		    "TO \"timezone\"") ||
	    !ast_deparse_preserves(
		    "DO LANGUAGE 'PLpgSQL' 'BEGIN NULL; END'",
		    "LANGUAGE 'PLpgSQL'",
		    NULL,
		    "LANGUAGE PLpgSQL") ||
	    !ast_deparse_preserves(
		    "COPY T (C) FROM STDIN CSV FREEZE",
		    "COPY T(C)",
		    "CSV FREEZE",
		    NULL)) {
		return 1;
	}
	return 0;
}
