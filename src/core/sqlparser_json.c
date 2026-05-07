#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <jansson.h>

#include "sqlparser_internal.h"

char *sqlparser_strndup_lower_ascii(const char *text, size_t len)
{
	char *copy;
	size_t index;

	copy = sqlparser_strndup(text, len);
	if (copy == NULL) {
		return NULL;
	}

	for (index = 0; index < len; index++) {
		copy[index] = (char)tolower((unsigned char)copy[index]);
	}

	return copy;
}

int sqlparser_json_array_contains_string(json_t *array, const char *value)
{
	size_t index;

	if (array == NULL || value == NULL) {
		return 0;
	}

	for (index = 0; index < json_array_size(array); index++) {
		json_t *entry;
		const char *entry_text;

		entry = json_array_get(array, index);
		if (!json_is_string(entry)) {
			continue;
		}

		entry_text = json_string_value(entry);
		if (entry_text != NULL && strcmp(entry_text, value) == 0) {
			return 1;
		}
	}

	return 0;
}

static int sqlparser_json_array_contains_table_name(json_t *array, const char *name)
{
	size_t index;

	if (array == NULL || name == NULL || name[0] == '\0') {
		return 0;
	}

	for (index = 0; index < json_array_size(array); index++) {
		json_t *entry;
		json_t *name_json;
		const char *entry_name;

		entry = json_array_get(array, index);
		if (!json_is_object(entry)) {
			continue;
		}

		name_json = json_object_get(entry, "name");
		if (!json_is_string(name_json)) {
			continue;
		}

		entry_name = json_string_value(name_json);
		if (entry_name != NULL && strcmp(entry_name, name) == 0) {
			return 1;
		}
	}

	return 0;
}

void sqlparser_json_object_set_nonempty_string(
	json_t *object,
	const char *key,
	const char *value)
{
	if (object == NULL || key == NULL || value == NULL || value[0] == '\0') {
		return;
	}

	(void)json_object_set_new(object, key, json_string(value));
}

sqlparser_status_t sqlparser_json_object_set_string(
	json_t *object,
	const char *key,
	const char *value,
	sqlparser_error_t *out_error)
{
	json_t *string_value;

	if (object == NULL || key == NULL || value == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"JSON string field requires non-NULL arguments");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	string_value = json_string(value);
	if (string_value == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}

	if (json_object_set_new(object, key, string_value) != 0) {
		json_decref(string_value);
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}

	return SQLPARSER_STATUS_OK;
}

void sqlparser_json_array_append_table(
	json_t *array,
	const char *schema_name,
	const char *table_name,
	const char *context)
{
	char *full_name;
	size_t full_name_len;
	json_t *entry;

	if (array == NULL || table_name == NULL || table_name[0] == '\0') {
		return;
	}

	full_name = NULL;
	if (schema_name != NULL && schema_name[0] != '\0') {
		full_name_len = strlen(schema_name) + 1U + strlen(table_name) + 1U;
		full_name = (char *)malloc(full_name_len);
		if (full_name == NULL) {
			return;
		}

		(void)snprintf(full_name, full_name_len, "%s.%s", schema_name, table_name);
	} else {
		full_name = sqlparser_strdup(table_name);
		if (full_name == NULL) {
			return;
		}
	}

	if (sqlparser_json_array_contains_table_name(array, full_name)) {
		free(full_name);
		return;
	}

	entry = json_object();
	if (entry == NULL) {
		free(full_name);
		return;
	}

	sqlparser_json_object_set_nonempty_string(entry, "name", full_name);
	sqlparser_json_object_set_nonempty_string(entry, "schema_name", schema_name);
	sqlparser_json_object_set_nonempty_string(entry, "table_name", table_name);
	sqlparser_json_object_set_nonempty_string(entry, "context", context);
	(void)json_array_append_new(array, entry);
	free(full_name);
}
