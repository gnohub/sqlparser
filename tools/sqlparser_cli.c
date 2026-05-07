#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <jansson.h>

#include "sqlparser/sqlparser.h"

typedef enum {
	SQLPARSER_CLI_MODE_ALL = 0,
	SQLPARSER_CLI_MODE_PARSE_TREE = 1,
	SQLPARSER_CLI_MODE_SUMMARY = 2,
	SQLPARSER_CLI_MODE_DEPARSE = 3,
	SQLPARSER_CLI_MODE_MODEL = 4
} sqlparser_cli_mode_t;

static void sqlparser_cli_error_clear(sqlparser_error_t *error)
{
	if (error == NULL) {
		return;
	}

	memset(error, 0, sizeof(*error));
	error->code = SQLPARSER_STATUS_OK;
}

static void sqlparser_cli_error_set(
	sqlparser_error_t *error,
	sqlparser_status_t code,
	const char *message)
{
	sqlparser_cli_error_clear(error);
	error->code = code;
	if (message != NULL) {
		(void)snprintf(error->message, sizeof(error->message), "%s", message);
	}
}

static const char *sqlparser_cli_mode_name(sqlparser_cli_mode_t mode)
{
	switch (mode) {
		case SQLPARSER_CLI_MODE_PARSE_TREE:
			return "parse-tree";
		case SQLPARSER_CLI_MODE_SUMMARY:
			return "summary";
		case SQLPARSER_CLI_MODE_DEPARSE:
			return "deparse";
		case SQLPARSER_CLI_MODE_MODEL:
			return "model";
		case SQLPARSER_CLI_MODE_ALL:
		default:
			return "all";
	}
}

static void sqlparser_cli_print_usage(const char *program)
{
	fprintf(
		stderr,
		"Usage: %s [--mode parse-tree|summary|deparse|model|all] [--dialect postgresql|mysql|oracle|sqlserver] [--compact] [--file PATH] [SQL]\n",
		program);
	fprintf(
		stderr,
		"       %s --batch-file PATH [--output PATH] [--mode parse-tree|summary|deparse|model|all] [--dialect postgresql|mysql|oracle|sqlserver] [--compact]\n",
		program);
	fprintf(stderr, "       %s --file ./tests/cases/sample.sql\n", program);
	fprintf(stderr, "       %s --batch-file ./tests/cases/sql_batch_input.json --output /tmp/out.json\n", program);
	fprintf(stderr, "       echo \"SELECT 1\" | %s --mode summary\n", program);
}

static int sqlparser_cli_ascii_equal_ci(const char *left, const char *right)
{
	size_t index;

	if (left == NULL || right == NULL) {
		return 0;
	}

	for (index = 0U; left[index] != '\0' || right[index] != '\0'; index++) {
		if (tolower((unsigned char)left[index]) != tolower((unsigned char)right[index])) {
			return 0;
		}
	}

	return 1;
}

static int sqlparser_cli_append(char **buffer, size_t *len, size_t *capacity, const char *data, size_t data_len)
{
	char *next;
	size_t required;
	size_t next_capacity;

	required = *len + data_len + 1U;
	if (required <= *capacity) {
		memcpy(*buffer + *len, data, data_len);
		*len += data_len;
		(*buffer)[*len] = '\0';
		return 0;
	}

	next_capacity = *capacity == 0U ? 4096U : *capacity;
	while (next_capacity < required) {
		if (next_capacity > ((size_t)-1) / 2U) {
			return -1;
		}
		next_capacity *= 2U;
	}

	next = (char *)realloc(*buffer, next_capacity);
	if (next == NULL) {
		return -1;
	}

	*buffer = next;
	*capacity = next_capacity;
	memcpy(*buffer + *len, data, data_len);
	*len += data_len;
	(*buffer)[*len] = '\0';
	return 0;
}

static char *sqlparser_cli_read_stream(FILE *stream)
{
	char chunk[4096];
	char *buffer;
	size_t len;
	size_t capacity;
	size_t read_size;

	buffer = NULL;
	len = 0U;
	capacity = 0U;

	for (;;) {
		read_size = fread(chunk, 1U, sizeof(chunk), stream);
		if (read_size > 0U) {
			if (sqlparser_cli_append(&buffer, &len, &capacity, chunk, read_size) != 0) {
				free(buffer);
				return NULL;
			}
		}

		if (read_size < sizeof(chunk)) {
			if (ferror(stream)) {
				free(buffer);
				return NULL;
			}
			break;
		}
	}

	if (buffer == NULL) {
		buffer = (char *)malloc(1U);
		if (buffer == NULL) {
			return NULL;
		}
		buffer[0] = '\0';
	}

	return buffer;
}

static char *sqlparser_cli_read_file(const char *path)
{
	FILE *input;
	char *content;

	input = fopen(path, "rb");
	if (input == NULL) {
		return NULL;
	}

	content = sqlparser_cli_read_stream(input);
	fclose(input);
	return content;
}

static int sqlparser_cli_write_file(const char *path, const char *content)
{
	FILE *output;
	size_t len;

	output = fopen(path, "wb");
	if (output == NULL) {
		return -1;
	}

	len = strlen(content);
	if (len > 0U && fwrite(content, 1U, len, output) != len) {
		fclose(output);
		return -1;
	}

	if (fclose(output) != 0) {
		return -1;
	}

	return 0;
}

static char *sqlparser_cli_strdup(const char *text)
{
	size_t len;
	char *copy;

	if (text == NULL) {
		return NULL;
	}

	len = strlen(text);
	copy = (char *)malloc(len + 1U);
	if (copy == NULL) {
		return NULL;
	}

	memcpy(copy, text, len + 1U);
	return copy;
}

static int sqlparser_cli_parse_mode(const char *value, sqlparser_cli_mode_t *mode_out)
{
	if (strcmp(value, "all") == 0) {
		*mode_out = SQLPARSER_CLI_MODE_ALL;
		return 0;
	}
	if (strcmp(value, "parse-tree") == 0) {
		*mode_out = SQLPARSER_CLI_MODE_PARSE_TREE;
		return 0;
	}
	if (strcmp(value, "summary") == 0) {
		*mode_out = SQLPARSER_CLI_MODE_SUMMARY;
		return 0;
	}
	if (strcmp(value, "deparse") == 0) {
		*mode_out = SQLPARSER_CLI_MODE_DEPARSE;
		return 0;
	}
	if (strcmp(value, "model") == 0) {
		*mode_out = SQLPARSER_CLI_MODE_MODEL;
		return 0;
	}

	return -1;
}

static int sqlparser_cli_parse_dialect(const char *value, sqlparser_dialect_t *dialect_out)
{
	if (value == NULL || dialect_out == NULL) {
		return -1;
	}

	if (sqlparser_cli_ascii_equal_ci(value, "postgresql") ||
	    sqlparser_cli_ascii_equal_ci(value, "postgres") ||
	    sqlparser_cli_ascii_equal_ci(value, "pg")) {
		*dialect_out = SQLPARSER_DIALECT_POSTGRESQL;
		return 0;
	}
	if (sqlparser_cli_ascii_equal_ci(value, "mysql")) {
		*dialect_out = SQLPARSER_DIALECT_MYSQL;
		return 0;
	}
	if (sqlparser_cli_ascii_equal_ci(value, "oracle")) {
		*dialect_out = SQLPARSER_DIALECT_ORACLE;
		return 0;
	}
	if (sqlparser_cli_ascii_equal_ci(value, "sqlserver") ||
	    sqlparser_cli_ascii_equal_ci(value, "mssql")) {
		*dialect_out = SQLPARSER_DIALECT_SQLSERVER;
		return 0;
	}

	return -1;
}

static int sqlparser_cli_print_json_section(
	const char *title,
	sqlparser_status_t (*export_fn)(
		const sqlparser_handle_t *handle,
		int pretty,
		char **out_json,
		sqlparser_error_t *out_error),
	const sqlparser_handle_t *handle,
	int pretty)
{
	sqlparser_error_t error;
	char *json_text;
	int status;

	json_text = NULL;
	status = export_fn(handle, pretty, &json_text, &error);
	if (status != SQLPARSER_STATUS_OK) {
		fprintf(stderr, "%s failed: %s\n", title, error.message);
		return 1;
	}

	printf("== %s ==\n%s\n", title, json_text);
	sqlparser_string_free(json_text);
	return 0;
}

static int sqlparser_cli_print_deparse(const sqlparser_handle_t *handle)
{
	sqlparser_error_t error;
	char *sql_text;
	int status;

	sql_text = NULL;
	status = sqlparser_deparse(handle, &sql_text, &error);
	if (status != SQLPARSER_STATUS_OK) {
		fprintf(stderr, "deparse failed: %s\n", error.message);
		return 1;
	}

	printf("== deparse ==\n%s\n", sql_text);
	sqlparser_string_free(sql_text);
	return 0;
}

static json_t *sqlparser_cli_error_to_json(const char *stage, const sqlparser_error_t *error)
{
	json_t *object;

	object = json_object();
	if (object == NULL) {
		return NULL;
	}

	if (stage != NULL) {
		(void)json_object_set_new(object, "stage", json_string(stage));
	}
	(void)json_object_set_new(object, "code", json_integer((json_int_t)error->code));
	(void)json_object_set_new(object, "cursor", json_integer((json_int_t)error->cursor));
	(void)json_object_set_new(object, "line", json_integer((json_int_t)error->line));
	(void)json_object_set_new(object, "column", json_integer((json_int_t)error->column));
	(void)json_object_set_new(object, "message", json_string(error->message));
	return object;
}

static json_t *sqlparser_cli_export_json_value(
	const sqlparser_handle_t *handle,
	sqlparser_status_t (*export_fn)(
		const sqlparser_handle_t *handle,
		int pretty,
		char **out_json,
		sqlparser_error_t *out_error),
	int pretty,
	sqlparser_error_t *error)
{
	char *json_text;
	json_t *value;
	json_error_t json_error;
	int status;

	json_text = NULL;
	status = export_fn(handle, pretty, &json_text, error);
	if (status != SQLPARSER_STATUS_OK) {
		return NULL;
	}

	value = json_loads(json_text, 0, &json_error);
	sqlparser_string_free(json_text);
	if (value == NULL) {
		sqlparser_cli_error_set(
			error,
			SQLPARSER_STATUS_INTERNAL_ERROR,
			"failed to decode exported JSON");
		return NULL;
	}

	return value;
}

static char *sqlparser_cli_export_deparse_text(
	const sqlparser_handle_t *handle,
	sqlparser_error_t *error)
{
	char *sql_text;
	int status;

	sql_text = NULL;
	status = sqlparser_deparse(handle, &sql_text, error);
	if (status != SQLPARSER_STATUS_OK) {
		return NULL;
	}

	return sql_text;
}

static int sqlparser_cli_extract_batch_item(
	json_t *item,
	const char **name_out,
	const char **sql_out,
	sqlparser_dialect_t default_dialect,
	sqlparser_dialect_t *dialect_out,
	sqlparser_error_t *error)
{
	json_t *dialect_json;
	json_t *name_json;
	json_t *sql_json;

	*name_out = NULL;
	*sql_out = NULL;
	*dialect_out = default_dialect;
	sqlparser_cli_error_clear(error);

	if (json_is_string(item)) {
		*sql_out = json_string_value(item);
		return 0;
	}

	if (!json_is_object(item)) {
		sqlparser_cli_error_set(
			error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"batch item must be a string or object");
		return -1;
	}

	name_json = json_object_get(item, "name");
	sql_json = json_object_get(item, "sql");
	dialect_json = json_object_get(item, "dialect");

	if (name_json != NULL && !json_is_string(name_json)) {
		sqlparser_cli_error_set(
			error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"batch item field 'name' must be a string");
		return -1;
	}

	if (!json_is_string(sql_json)) {
		sqlparser_cli_error_set(
			error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"batch item field 'sql' must be a string");
		return -1;
	}
	if (dialect_json != NULL) {
		if (!json_is_string(dialect_json)) {
			sqlparser_cli_error_set(
				error,
				SQLPARSER_STATUS_INVALID_ARGUMENT,
				"batch item field 'dialect' must be a string");
			return -1;
		}
		if (sqlparser_cli_parse_dialect(json_string_value(dialect_json), dialect_out) != 0) {
			sqlparser_cli_error_set(
				error,
				SQLPARSER_STATUS_INVALID_ARGUMENT,
				"batch item field 'dialect' is not supported");
			return -1;
		}
	}

	*name_out = name_json != NULL ? json_string_value(name_json) : NULL;
	*sql_out = json_string_value(sql_json);
	return 0;
}

static json_t *sqlparser_cli_process_batch_entry(
	size_t index,
	const char *name,
	const char *sql,
	sqlparser_dialect_t dialect,
	sqlparser_cli_mode_t mode,
	int pretty,
	int *ok_out)
{
	json_t *entry;
	sqlparser_handle_t *handle;
	sqlparser_parse_options_t options;
	sqlparser_error_t error;
	int status;

	entry = json_object();
	if (entry == NULL) {
		return NULL;
	}

	handle = NULL;
	sqlparser_cli_error_clear(&error);
	*ok_out = 0;

	(void)json_object_set_new(entry, "index", json_integer((json_int_t)(index + 1U)));
	if (name != NULL && name[0] != '\0') {
		(void)json_object_set_new(entry, "name", json_string(name));
	}
	if (sql != NULL) {
		(void)json_object_set_new(entry, "sql", json_string(sql));
	}
	(void)json_object_set_new(entry, "dialect", json_string(sqlparser_dialect_name(dialect)));

	sqlparser_parse_options_default(&options);
	options.dialect = dialect;
	status = sqlparser_parse_with_options(sql, &options, &handle, &error);
	if (status != SQLPARSER_STATUS_OK) {
		(void)json_object_set_new(entry, "ok", json_false());
		(void)json_object_set_new(entry, "error", sqlparser_cli_error_to_json("parse", &error));
		return entry;
	}

	switch (mode) {
		case SQLPARSER_CLI_MODE_PARSE_TREE:
		{
			json_t *parse_tree;

			parse_tree = sqlparser_cli_export_json_value(
				handle,
				sqlparser_export_parse_tree_json,
				pretty,
				&error);
			if (parse_tree == NULL) {
				(void)json_object_set_new(entry, "ok", json_false());
				(void)json_object_set_new(entry, "error", sqlparser_cli_error_to_json("parse-tree", &error));
				sqlparser_handle_destroy(handle);
				return entry;
			}
			(void)json_object_set_new(entry, "parse_tree", parse_tree);
			break;
		}
		case SQLPARSER_CLI_MODE_SUMMARY:
		{
			json_t *summary;

			summary = sqlparser_cli_export_json_value(
				handle,
				sqlparser_export_summary_json,
				pretty,
				&error);
			if (summary == NULL) {
				(void)json_object_set_new(entry, "ok", json_false());
				(void)json_object_set_new(entry, "error", sqlparser_cli_error_to_json("summary", &error));
				sqlparser_handle_destroy(handle);
				return entry;
			}
			(void)json_object_set_new(entry, "summary", summary);
			break;
		}
		case SQLPARSER_CLI_MODE_DEPARSE:
		{
			char *deparse_sql;

			deparse_sql = sqlparser_cli_export_deparse_text(handle, &error);
			if (deparse_sql == NULL) {
				(void)json_object_set_new(entry, "ok", json_false());
				(void)json_object_set_new(entry, "error", sqlparser_cli_error_to_json("deparse", &error));
				sqlparser_handle_destroy(handle);
				return entry;
			}
			(void)json_object_set_new(entry, "deparse_sql", json_string(deparse_sql));
			sqlparser_string_free(deparse_sql);
			break;
		}
		case SQLPARSER_CLI_MODE_MODEL:
		{
			json_t *model;

			model = sqlparser_cli_export_json_value(
				handle,
				sqlparser_export_model_json,
				pretty,
				&error);
			if (model == NULL) {
				(void)json_object_set_new(entry, "ok", json_false());
				(void)json_object_set_new(entry, "error", sqlparser_cli_error_to_json("model", &error));
				sqlparser_handle_destroy(handle);
				return entry;
			}
			(void)json_object_set_new(entry, "model", model);
			break;
		}
		case SQLPARSER_CLI_MODE_ALL:
		default:
		{
			json_t *parse_tree;
			json_t *summary;
			json_t *model;
			char *deparse_sql;

			parse_tree = sqlparser_cli_export_json_value(
				handle,
				sqlparser_export_parse_tree_json,
				pretty,
				&error);
			if (parse_tree == NULL) {
				(void)json_object_set_new(entry, "ok", json_false());
				(void)json_object_set_new(entry, "error", sqlparser_cli_error_to_json("parse-tree", &error));
				sqlparser_handle_destroy(handle);
				return entry;
			}

			summary = sqlparser_cli_export_json_value(
				handle,
				sqlparser_export_summary_json,
				pretty,
				&error);
			if (summary == NULL) {
				json_decref(parse_tree);
				(void)json_object_set_new(entry, "ok", json_false());
				(void)json_object_set_new(entry, "error", sqlparser_cli_error_to_json("summary", &error));
				sqlparser_handle_destroy(handle);
				return entry;
			}

			deparse_sql = sqlparser_cli_export_deparse_text(handle, &error);
			if (deparse_sql == NULL) {
				json_decref(parse_tree);
				json_decref(summary);
				(void)json_object_set_new(entry, "ok", json_false());
				(void)json_object_set_new(entry, "error", sqlparser_cli_error_to_json("deparse", &error));
				sqlparser_handle_destroy(handle);
				return entry;
			}

			model = sqlparser_cli_export_json_value(
				handle,
				sqlparser_export_model_json,
				pretty,
				&error);
			if (model == NULL) {
				json_decref(parse_tree);
				json_decref(summary);
				sqlparser_string_free(deparse_sql);
				(void)json_object_set_new(entry, "ok", json_false());
				(void)json_object_set_new(entry, "error", sqlparser_cli_error_to_json("model", &error));
				sqlparser_handle_destroy(handle);
				return entry;
			}

			(void)json_object_set_new(entry, "parse_tree", parse_tree);
			(void)json_object_set_new(entry, "summary", summary);
			(void)json_object_set_new(entry, "model", model);
			(void)json_object_set_new(entry, "deparse_sql", json_string(deparse_sql));
			sqlparser_string_free(deparse_sql);
			break;
		}
	}

	(void)json_object_set_new(entry, "ok", json_true());
	*ok_out = 1;
	sqlparser_handle_destroy(handle);
	return entry;
}

static json_t *sqlparser_cli_find_batch_items(json_t *root)
{
	json_t *items;

	if (json_is_array(root)) {
		return root;
	}

	if (!json_is_object(root)) {
		return NULL;
	}

	items = json_object_get(root, "items");
	if (json_is_array(items)) {
		return items;
	}

	items = json_object_get(root, "sqls");
	if (json_is_array(items)) {
		return items;
	}

	return NULL;
}

static int sqlparser_cli_run_batch(
	const char *batch_file_path,
	const char *output_path,
	sqlparser_dialect_t default_dialect,
	sqlparser_cli_mode_t mode,
	int pretty)
{
	json_t *input_root;
	json_t *items;
	json_t *output_root;
	json_t *output_items;
	json_error_t json_error;
	size_t index;
	size_t total;
	size_t succeeded;
	size_t failed;
	sqlparser_dialect_t batch_dialect;
	char *rendered;
	size_t flags;

	input_root = json_load_file(batch_file_path, 0, &json_error);
	if (input_root == NULL) {
		fprintf(
			stderr,
			"failed to load batch JSON file %s: line %d: %s\n",
			batch_file_path,
			json_error.line,
			json_error.text);
		return 1;
	}

	items = sqlparser_cli_find_batch_items(input_root);
	if (items == NULL) {
		json_decref(input_root);
		fprintf(stderr, "batch JSON must be an array or an object with 'items'/'sqls' array\n");
		return 1;
	}

	output_root = json_object();
	output_items = json_array();
	if (output_root == NULL || output_items == NULL) {
		json_decref(input_root);
		if (output_root != NULL) {
			json_decref(output_root);
		}
		if (output_items != NULL) {
			json_decref(output_items);
		}
		fprintf(stderr, "out of memory\n");
		return 1;
	}

	total = json_array_size(items);
	succeeded = 0U;
	failed = 0U;
	batch_dialect = default_dialect;
	if (json_is_object(input_root)) {
		json_t *dialect_json;

		dialect_json = json_object_get(input_root, "dialect");
		if (dialect_json != NULL) {
			if (!json_is_string(dialect_json) ||
			    sqlparser_cli_parse_dialect(json_string_value(dialect_json), &batch_dialect) != 0) {
				json_decref(input_root);
				json_decref(output_root);
				fprintf(stderr, "batch field 'dialect' must be one of postgresql/mysql/oracle/sqlserver\n");
				return 1;
			}
		}
	}

	(void)json_object_set_new(output_root, "mode", json_string(sqlparser_cli_mode_name(mode)));
	(void)json_object_set_new(output_root, "dialect", json_string(sqlparser_dialect_name(batch_dialect)));
	(void)json_object_set_new(output_root, "source_file", json_string(batch_file_path));
	(void)json_object_set_new(output_root, "items", output_items);

	for (index = 0; index < total; index++) {
		json_t *item;
		json_t *entry;
		const char *name;
		const char *sql;
		sqlparser_dialect_t item_dialect;
		sqlparser_error_t error;
		int ok;

		item = json_array_get(items, index);
		name = NULL;
		sql = NULL;
		item_dialect = batch_dialect;
		sqlparser_cli_error_clear(&error);

		if (sqlparser_cli_extract_batch_item(item, &name, &sql, batch_dialect, &item_dialect, &error) != 0) {
			entry = json_object();
			if (entry == NULL) {
				json_decref(input_root);
				json_decref(output_root);
				fprintf(stderr, "out of memory\n");
				return 1;
			}

			(void)json_object_set_new(entry, "index", json_integer((json_int_t)(index + 1U)));
			(void)json_object_set_new(entry, "ok", json_false());
			(void)json_object_set_new(entry, "error", sqlparser_cli_error_to_json("input", &error));
			(void)json_array_append_new(output_items, entry);
			failed++;
			continue;
		}

		entry = sqlparser_cli_process_batch_entry(index, name, sql, item_dialect, mode, pretty, &ok);
		if (entry == NULL) {
			json_decref(input_root);
			json_decref(output_root);
			fprintf(stderr, "out of memory\n");
			return 1;
		}

		(void)json_array_append_new(output_items, entry);
		if (ok) {
			succeeded++;
		} else {
			failed++;
		}
	}

	(void)json_object_set_new(output_root, "total", json_integer((json_int_t)total));
	(void)json_object_set_new(output_root, "succeeded", json_integer((json_int_t)succeeded));
	(void)json_object_set_new(output_root, "failed", json_integer((json_int_t)failed));
	(void)json_object_set_new(output_root, "has_failures", failed > 0U ? json_true() : json_false());

	flags = JSON_ENSURE_ASCII | JSON_SORT_KEYS;
	if (pretty) {
		flags |= JSON_INDENT(2);
	} else {
		flags |= JSON_COMPACT;
	}

	rendered = json_dumps(output_root, flags);
	json_decref(input_root);
	json_decref(output_root);
	if (rendered == NULL) {
		fprintf(stderr, "failed to render batch result JSON\n");
		return 1;
	}

	if (output_path != NULL) {
		if (sqlparser_cli_write_file(output_path, rendered) != 0) {
			free(rendered);
			fprintf(stderr, "failed to write output file: %s\n", output_path);
			return 1;
		}
	} else {
		printf("%s\n", rendered);
	}

	free(rendered);
	return 0;
}

int main(int argc, char **argv)
{
	sqlparser_cli_mode_t mode;
	sqlparser_dialect_t dialect;
	const char *file_path;
	const char *batch_file_path;
	const char *output_path;
	const char *sql_arg;
	char *sql_text;
	sqlparser_handle_t *handle;
	sqlparser_error_t error;
	int pretty;
	int index;
	int status;

	mode = SQLPARSER_CLI_MODE_ALL;
	dialect = SQLPARSER_DIALECT_POSTGRESQL;
	file_path = NULL;
	batch_file_path = NULL;
	output_path = NULL;
	sql_arg = NULL;
	sql_text = NULL;
	handle = NULL;
	pretty = 1;

	for (index = 1; index < argc; index++) {
		if (strcmp(argv[index], "--mode") == 0) {
			if (index + 1 >= argc) {
				sqlparser_cli_print_usage(argv[0]);
				return 2;
			}
			if (sqlparser_cli_parse_mode(argv[index + 1], &mode) != 0) {
				fprintf(stderr, "unsupported mode: %s\n", argv[index + 1]);
				return 2;
			}
			index++;
			continue;
		}
		if (strcmp(argv[index], "--compact") == 0) {
			pretty = 0;
			continue;
		}
		if (strcmp(argv[index], "--dialect") == 0) {
			if (index + 1 >= argc) {
				sqlparser_cli_print_usage(argv[0]);
				return 2;
			}
			if (sqlparser_cli_parse_dialect(argv[index + 1], &dialect) != 0) {
				fprintf(stderr, "unsupported dialect: %s\n", argv[index + 1]);
				return 2;
			}
			index++;
			continue;
		}
		if (strcmp(argv[index], "--file") == 0) {
			if (index + 1 >= argc) {
				sqlparser_cli_print_usage(argv[0]);
				return 2;
			}
			file_path = argv[index + 1];
			index++;
			continue;
		}
		if (strcmp(argv[index], "--batch-file") == 0) {
			if (index + 1 >= argc) {
				sqlparser_cli_print_usage(argv[0]);
				return 2;
			}
			batch_file_path = argv[index + 1];
			index++;
			continue;
		}
		if (strcmp(argv[index], "--output") == 0) {
			if (index + 1 >= argc) {
				sqlparser_cli_print_usage(argv[0]);
				return 2;
			}
			output_path = argv[index + 1];
			index++;
			continue;
		}
		if (strcmp(argv[index], "--help") == 0 || strcmp(argv[index], "-h") == 0) {
			sqlparser_cli_print_usage(argv[0]);
			return 0;
		}
		if (argv[index][0] == '-' && argv[index][1] != '\0') {
			fprintf(stderr, "unsupported option: %s\n", argv[index]);
			return 2;
		}
		sql_arg = argv[index];
		break;
	}

	if (batch_file_path != NULL) {
		if (file_path != NULL || sql_arg != NULL) {
			fprintf(stderr, "--batch-file cannot be combined with --file or inline SQL\n");
			return 2;
		}
		return sqlparser_cli_run_batch(batch_file_path, output_path, dialect, mode, pretty);
	}

	if (output_path != NULL) {
		fprintf(stderr, "--output currently requires --batch-file\n");
		return 2;
	}

	if (file_path != NULL && sql_arg != NULL) {
		fprintf(stderr, "--file and inline SQL cannot be used together\n");
		return 2;
	}

	if (file_path != NULL) {
		sql_text = sqlparser_cli_read_file(file_path);
		if (sql_text == NULL) {
			fprintf(stderr, "failed to read file: %s\n", file_path);
			return 1;
		}
	} else if (sql_arg != NULL) {
		sql_text = sqlparser_cli_strdup(sql_arg);
		if (sql_text == NULL) {
			fprintf(stderr, "out of memory\n");
			return 1;
		}
	} else {
		sql_text = sqlparser_cli_read_stream(stdin);
		if (sql_text == NULL) {
			fprintf(stderr, "failed to read SQL from stdin\n");
			return 1;
		}
	}

	{
		sqlparser_parse_options_t options;

		sqlparser_parse_options_default(&options);
		options.dialect = dialect;
		status = sqlparser_parse_with_options(sql_text, &options, &handle, &error);
	}
	if (status != SQLPARSER_STATUS_OK) {
		fprintf(
			stderr,
			"parse failed: code=%d cursor=%d line=%d column=%d message=%s\n",
			(int)error.code,
			error.cursor,
			error.line,
			error.column,
			error.message);
		free(sql_text);
		return 1;
	}

	switch (mode) {
		case SQLPARSER_CLI_MODE_PARSE_TREE:
			status = sqlparser_cli_print_json_section(
				"parse_tree_json",
				sqlparser_export_parse_tree_json,
				handle,
				pretty);
			break;
		case SQLPARSER_CLI_MODE_SUMMARY:
			status = sqlparser_cli_print_json_section(
				"summary_json",
				sqlparser_export_summary_json,
				handle,
				pretty);
			break;
		case SQLPARSER_CLI_MODE_DEPARSE:
			status = sqlparser_cli_print_deparse(handle);
			break;
		case SQLPARSER_CLI_MODE_MODEL:
			status = sqlparser_cli_print_json_section(
				"model_json",
				sqlparser_export_model_json,
				handle,
				pretty);
			break;
		case SQLPARSER_CLI_MODE_ALL:
		default:
			status = 0;
			if (sqlparser_cli_print_json_section(
					"parse_tree_json",
					sqlparser_export_parse_tree_json,
					handle,
					pretty) != 0) {
				status = 1;
			}
			if (status == 0 &&
				sqlparser_cli_print_json_section(
					"summary_json",
					sqlparser_export_summary_json,
					handle,
					pretty) != 0) {
				status = 1;
			}
			if (status == 0 &&
				sqlparser_cli_print_json_section(
					"model_json",
					sqlparser_export_model_json,
					handle,
					pretty) != 0) {
				status = 1;
			}
			if (status == 0 && sqlparser_cli_print_deparse(handle) != 0) {
				status = 1;
			}
			break;
	}

	sqlparser_handle_destroy(handle);
	free(sql_text);
	return status;
}
